/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Motion sense module to read from various motion sensors. */

#include "accelgyro.h"
#include "atomic.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gesture.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "lid_angle.h"
#include "lightbar.h"
#include "math_util.h"
#include "mkbp_event.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "power.h"
#include "queue.h"
#include "timer.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_MOTION_SENSE, outstr)
#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

/*
 * Sampling interval for measuring acceleration and calculating lid angle.
 */
test_export_static unsigned int motion_interval;

/* Delay between FIFO interruption. */
static unsigned int motion_int_interval;

/* Minimum time in between running motion sense task loop. */
unsigned int motion_min_interval = CONFIG_MOTION_MIN_SENSE_WAIT_TIME * MSEC;
#ifdef CONFIG_CMD_ACCEL_INFO
static int accel_disp;
#endif

#define SENSOR_ACTIVE(_sensor) (sensor_active & (_sensor)->active_mask)

#if defined(CONFIG_LPC) || defined(TEST_MOTION_LID)
#define UPDATE_HOST_MEM_MAP
#endif

/*
 * Adjustment in us to ec rate when calculating interrupt interval:
 * To be sure the EC will send an interrupt even if it finishes processing
 * events slightly earlier than the previous period.
 */
#define MOTION_SENSOR_INT_ADJUSTMENT_US 10

/*
 * Mutex to protect sensor values between host command task and
 * motion sense task:
 * When we process CMD_DUMP, we want to be sure the motion sense
 * task is not updating the sensor values at the same time.
 */
static struct mutex g_sensor_mutex;

/*
 * Current power level (S0, S3, S5, ...)
 */
test_export_static enum chipset_state_mask sensor_active;

#ifdef CONFIG_ACCEL_FIFO
/* Need to wake up the AP */
static int wake_up_needed;

/* Need to send flush events */
static int fifo_flush_needed;
/* Number of element the AP should collect */
static int fifo_queue_count;
static int fifo_int_enabled;

struct queue motion_sense_fifo = QUEUE_NULL(CONFIG_ACCEL_FIFO,
		struct ec_response_motion_sensor_data);
static int motion_sense_fifo_lost;

static void motion_sense_insert_timestamp(void);

void motion_sense_fifo_add_unit(struct ec_response_motion_sensor_data *data,
				struct motion_sensor_t *sensor,
				int valid_data)
{
	struct ec_response_motion_sensor_data vector;
	int i;

	mutex_lock(&g_sensor_mutex);
	if (queue_space(&motion_sense_fifo) == 0) {
		queue_remove_unit(&motion_sense_fifo, &vector);
		motion_sense_fifo_lost++;
		motion_sensors[vector.sensor_num].lost++;
	}
	for (i = 0; i < valid_data; i++)
		sensor->xyz[i] = data->data[i];

	/* For valid sensors, check if AP really needs this data */
	if (valid_data) {
		int removed;

		if (sensor->oversampling_ratio == 0) {
			mutex_unlock(&g_sensor_mutex);
			return;
		}
		removed = sensor->oversampling++;
		sensor->oversampling %= sensor->oversampling_ratio;
		if (removed != 0) {
			mutex_unlock(&g_sensor_mutex);
			return;
		}
	}
	mutex_unlock(&g_sensor_mutex);
	if (data->flags & MOTIONSENSE_SENSOR_FLAG_WAKEUP) {
		/*
		 * Fist, send a timestamp to be sure the event will not
		 * be tied to an old one.
		 */
		motion_sense_insert_timestamp();
		wake_up_needed = 1;
	}
	mutex_lock(&g_sensor_mutex);
	queue_add_unit(&motion_sense_fifo, data);
	mutex_unlock(&g_sensor_mutex);
}

static void motion_sense_insert_flush(struct motion_sensor_t *sensor)
{
	struct ec_response_motion_sensor_data vector;
	vector.flags = MOTIONSENSE_SENSOR_FLAG_FLUSH |
		       MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
	vector.timestamp = __hw_clock_source_read();
	vector.sensor_num = sensor - motion_sensors;

	motion_sense_fifo_add_unit(&vector, sensor, 0);
}

static void motion_sense_insert_timestamp(void)
{
	struct ec_response_motion_sensor_data vector;
	vector.flags = MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
	vector.timestamp = __hw_clock_source_read();
	vector.sensor_num = 0;
	motion_sense_fifo_add_unit(&vector, NULL, 0);
}

static void motion_sense_get_fifo_info(
		struct ec_response_motion_sense_fifo_info *fifo_info)
{
	fifo_info->size = motion_sense_fifo.buffer_units;
	mutex_lock(&g_sensor_mutex);
	fifo_info->count = fifo_queue_count;
	fifo_info->total_lost = motion_sense_fifo_lost;
	mutex_unlock(&g_sensor_mutex);
	fifo_info->timestamp = __hw_clock_source_read();
}
#endif

/* Minimal amount of time since last collection before triggering a new one */
static inline int motion_sensor_time_to_read(const timestamp_t *ts,
		const struct motion_sensor_t *sensor)
{
	int rate_mhz = sensor->drv->get_data_rate(sensor);

	if (rate_mhz == 0)
		return 0;
	/*
	 * converting from kHz to us.
	 * If within 95% of the time, check sensor.
	 */
	return time_after(ts->le.lo,
			  sensor->last_collection + SECOND * 950 / rate_mhz);
}

static enum sensor_config motion_sense_get_ec_config(void)
{
	switch (sensor_active) {
	case SENSOR_ACTIVE_S0:
		return SENSOR_CONFIG_EC_S0;
	case SENSOR_ACTIVE_S3:
		return SENSOR_CONFIG_EC_S3;
	case SENSOR_ACTIVE_S5:
		return SENSOR_CONFIG_EC_S5;
	default:
		CPRINTS("get_ec_config: Invalid active state: %x",
			sensor_active);
		return SENSOR_CONFIG_MAX;
	}
}
/* motion_sense_set_data_rate
 *
 * Set the sensor data rate. It is altered when the AP change the data
 * rate or when the power state changes.
 */
int motion_sense_set_data_rate(struct motion_sensor_t *sensor)
{
	int roundup, ap_odr_mhz = 0, ec_odr_mhz, odr, ret;
	enum sensor_config config_id;
	timestamp_t ts = get_time();

	/* We assume the sensor is initialized */

	/* Check the AP setting first. */
	if (sensor_active != SENSOR_ACTIVE_S5)
		ap_odr_mhz = BASE_ODR(sensor->config[SENSOR_CONFIG_AP].odr);

	/* check if the EC set the sensor ODR at a higher frequency */
	config_id = motion_sense_get_ec_config();
	ec_odr_mhz = BASE_ODR(sensor->config[config_id].odr);
	if (ec_odr_mhz > ap_odr_mhz) {
		odr = ec_odr_mhz;
	} else {
		odr = ap_odr_mhz;
		config_id = SENSOR_CONFIG_AP;
	}
	roundup = !!(sensor->config[config_id].odr & ROUND_UP_FLAG);
	ret = sensor->drv->set_data_rate(sensor, odr, roundup);
	if (ret)
		return ret;

	CPRINTS("%s ODR: %d - roundup %d from config %d [AP %d]",
		sensor->name, odr, roundup, config_id,
		BASE_ODR(sensor->config[SENSOR_CONFIG_AP].odr));
	mutex_lock(&g_sensor_mutex);
	if (ap_odr_mhz)
		sensor->oversampling_ratio =
			sensor->drv->get_data_rate(sensor) / ap_odr_mhz;
	else
		sensor->oversampling_ratio = 0;

	/*
	 * Reset last collection: the last collection may be so much in the past
	 * it may appear to be in the future.
	 */
	sensor->last_collection = ts.le.lo;
	sensor->oversampling = 0;
	mutex_unlock(&g_sensor_mutex);
	return 0;
}

static int motion_sense_set_ec_rate_from_ap(
		const struct motion_sensor_t *sensor,
		unsigned int new_rate_us)
{
	int odr_mhz = sensor->drv->get_data_rate(sensor);

	if (new_rate_us == 0)
		return 0;
#ifdef CONFIG_ACCEL_FORCE_MODE_MASK
	if (CONFIG_ACCEL_FORCE_MODE_MASK & (1 << (sensor - motion_sensors)))
		goto end_set_ec_rate_from_ap;
#endif
	if (odr_mhz == 0)
		goto end_set_ec_rate_from_ap;

	/*
	 * If the EC collection rate is close to the sensor data rate,
	 * given variation from the EC scheduler, it is possible that a sensor
	 * will not present any measurement for a given time slice, and then 2
	 * measurement for the next. That will create a large interval between
	 * 2 measurements.
	 * To prevent that, increase the EC period by 5% to be sure to get at
	 * least one measurement at every collection time.
	 * We will apply that correction only if the ec rate is within 10% of
	 * the data rate.
	 */
	if (SECOND * 1100 / odr_mhz > new_rate_us)
		new_rate_us = new_rate_us / 100 * 105;

end_set_ec_rate_from_ap:
	return MAX(new_rate_us, motion_min_interval);
}


/*
 * motion_sense_select_ec_rate
 *
 * Calculate the ec_rate for a given sensor.
 * - sensor: sensor to use
 * - config_id: determine the requester (AP or EC).
 * - interrupt:
 * If interrupt is set: return the sampling rate requested by AP or EC.
 * If interrupt is not set and the sensor is in forced mode,
 * we return the rate needed to probe the sensor at the right ODR.
 * otherwise return the sampling rate requested by AP or EC.
 *
 * return rate in us.
 */
static int motion_sense_select_ec_rate(
		const struct motion_sensor_t *sensor,
		enum sensor_config config_id,
		int interrupt)
{
#ifdef CONFIG_ACCEL_FORCE_MODE_MASK
	if (interrupt == 0 &&
	    (CONFIG_ACCEL_FORCE_MODE_MASK & (1 << (sensor - motion_sensors)))) {
		int rate_mhz = BASE_ODR(sensor->config[config_id].odr);
		/* we have to run ec at the sensor frequency rate.*/
		if (rate_mhz > 0)
			return SECOND * 1000 / rate_mhz;
		else
			return 0;
	} else
#endif
	return sensor->config[config_id].ec_rate;
}

/* motion_sense_ec_rate
 *
 * Calculate the sensor ec rate. It will be use to set the motion task polling
 * rate.
 *
 * Return the EC rate, in us.
 */
static int motion_sense_ec_rate(struct motion_sensor_t *sensor)
{
	int ec_rate = 0, ec_rate_from_cfg;

	/* Check the AP setting first. */
	if (sensor_active != SENSOR_ACTIVE_S5)
		ec_rate = motion_sense_select_ec_rate(
			sensor, SENSOR_CONFIG_AP, 0);

	ec_rate_from_cfg = motion_sense_select_ec_rate(
			sensor, motion_sense_get_ec_config(), 0);

	if (ec_rate_from_cfg != 0)
		if (ec_rate == 0 || ec_rate_from_cfg < ec_rate)
			ec_rate = ec_rate_from_cfg;
	return ec_rate;
}

/*
 * motion_sense_set_motion_intervals
 *
 * Set the wake up interval for the motion sense thread.
 * It is set to the highest frequency one of the sensors need to be polled at.
 *
 * Note: Not static to be tested.
 */
static int motion_sense_set_motion_intervals(void)
{
	int i, sensor_ec_rate, ec_rate = 0, ec_int_rate = 0;
	struct motion_sensor_t *sensor;
	for (i = 0; i < motion_sensor_count; ++i) {
		sensor = &motion_sensors[i];
		/*
		 * If the sensor is sleeping, no need to check it periodically.
		 */
		if ((sensor->state != SENSOR_INITIALIZED) ||
		    (sensor->drv->get_data_rate(sensor) == 0))
			continue;

		sensor_ec_rate = motion_sense_ec_rate(sensor);
		if (sensor_ec_rate == 0)
			continue;
		if (ec_rate == 0 || sensor_ec_rate < ec_rate)
			ec_rate = sensor_ec_rate;

		sensor_ec_rate = motion_sense_select_ec_rate(
				sensor, SENSOR_CONFIG_AP, 1);
		if (ec_int_rate == 0 ||
		    (sensor_ec_rate && sensor_ec_rate < ec_int_rate))
			ec_int_rate = sensor_ec_rate;
	}
	motion_interval = ec_rate;

	motion_int_interval =
		MAX(0, ec_int_rate - MOTION_SENSOR_INT_ADJUSTMENT_US);
	/*
	 * Wake up the motion sense task: we want to sensor task to take
	 * in account the new period right away.
	 */
	task_wake(TASK_ID_MOTIONSENSE);
	return motion_interval;
}

static inline int motion_sense_init(struct motion_sensor_t *sensor)
{
	int ret, cnt = 3;

	/* Initialize accelerometers. */
	do {
		ret = sensor->drv->init(sensor);
	} while ((ret != EC_SUCCESS) && (--cnt > 0));

	if (ret != EC_SUCCESS) {
		sensor->state = SENSOR_INIT_ERROR;
	} else {
		sensor->state = SENSOR_INITIALIZED;
		motion_sense_set_data_rate(sensor);
	}
	return ret;
}

/*
 * motion_sense_switch_sensor_rate
 *
 * Suspend all sensors that are not needed.
 * Mark them as uninitialized, they will lose power and
 * need to be initialized again.
 */
static void motion_sense_switch_sensor_rate(void)
{
	int i, ret;
	struct motion_sensor_t *sensor;
	for (i = 0; i < motion_sensor_count; ++i) {
		sensor = &motion_sensors[i];
		if (SENSOR_ACTIVE(sensor)) {
			/* Initialize or just back the odr previously set. */
			if (sensor->state == SENSOR_INITIALIZED) {
				motion_sense_set_data_rate(sensor);
			} else {
				ret = motion_sense_init(sensor);
				if (ret != EC_SUCCESS) {
					CPRINTS("%s: %d: init failed: %d",
						sensor->name, i, ret);
				}
			}
		} else {
			/* The sensors are being powered off */
			if (sensor->state == SENSOR_INITIALIZED)
				sensor->state = SENSOR_NOT_INITIALIZED;
		}
	}
	motion_sense_set_motion_intervals();
}

static void motion_sense_shutdown(void)
{
	int i;
	struct motion_sensor_t *sensor;
#ifdef CONFIG_GESTURE_DETECTION_MASK
	uint32_t enabled = 0, disabled, mask;
#endif

	sensor_active = SENSOR_ACTIVE_S5;
	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		/* Forget about changes made by the AP */
		sensor->config[SENSOR_CONFIG_AP].odr = 0;
		sensor->config[SENSOR_CONFIG_AP].ec_rate = 0;
	}
	motion_sense_switch_sensor_rate();

	for (i = 0; i < motion_sensor_count; i++) {
		sensor = &motion_sensors[i];
		if (sensor->state != SENSOR_INITIALIZED)
			continue;
		sensor->drv->set_range(sensor, sensor->default_range, 0);
	}
	/* Forget activities set by the AP */
#ifdef CONFIG_GESTURE_DETECTION_MASK
	mask = CONFIG_GESTURE_DETECTION_MASK;
	while (mask) {
		i = get_next_bit(&mask);
		sensor = &motion_sensors[i];
		if (sensor->state != SENSOR_INITIALIZED)
			continue;
		sensor->drv->list_activities(sensor,
				&enabled, &disabled);
		/* exclude double tap, it is used internally. */
		enabled &= ~(1 << MOTIONSENSE_ACTIVITY_DOUBLE_TAP);
		while (enabled) {
			int activity = get_next_bit(&enabled);
			sensor->drv->manage_activity(sensor, activity, 0, NULL);
		}
		/* Re-enable double tap in case AP disabled it */
		sensor->drv->manage_activity(sensor,
				MOTIONSENSE_ACTIVITY_DOUBLE_TAP, 1, NULL);
	}
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, motion_sense_shutdown,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_suspend(void)
{
	/*
	 *  If we are coming from S5, don't enter suspend:
	 *  We will go in SO almost immediately.
	 */
	if (sensor_active == SENSOR_ACTIVE_S5)
		return;

	sensor_active = SENSOR_ACTIVE_S3;
	motion_sense_switch_sensor_rate();
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, motion_sense_suspend,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_resume(void)
{
	sensor_active = SENSOR_ACTIVE_S0;
	motion_sense_switch_sensor_rate();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, motion_sense_resume,
	     MOTION_SENSE_HOOK_PRIO);

static void motion_sense_startup(void)
{
	/*
	 * If the AP is already in S0, call the resume hook now.
	 * We may initialize the sensor 2 times (once in RO, another time in
	 * RW), but it may be necessary if the init sequence has changed.
	 */
	if (chipset_in_state(SENSOR_ACTIVE_S0_S3_S5))
		motion_sense_shutdown();
	if (chipset_in_state(SENSOR_ACTIVE_S0_S3))
		motion_sense_suspend();
	if (chipset_in_state(SENSOR_ACTIVE_S0))
		motion_sense_resume();
}
DECLARE_HOOK(HOOK_INIT, motion_sense_startup,
	     MOTION_SENSE_HOOK_PRIO);

/* Write to LPC status byte to represent that accelerometers are present. */
static inline void set_present(uint8_t *lpc_status)
{
	*lpc_status |= EC_MEMMAP_ACC_STATUS_PRESENCE_BIT;
}

#ifdef UPDATE_HOST_MEM_MAP
/* Update/Write LPC data */
static inline void update_sense_data(uint8_t *lpc_status,
		uint16_t *lpc_data, int *psample_id)
{
	int i;
	struct motion_sensor_t *sensor;
	/*
	 * Set the busy bit before writing the sensor data. Increment
	 * the counter and clear the busy bit after writing the sensor
	 * data. On the host side, the host needs to make sure the busy
	 * bit is not set and that the counter remains the same before
	 * and after reading the data.
	 */
	*lpc_status |= EC_MEMMAP_ACC_STATUS_BUSY_BIT;

	/*
	 * Copy sensor data to shared memory. Note that this code
	 * assumes little endian, which is what the host expects. Also,
	 * note that we share the lid angle calculation with host only
	 * for debugging purposes. The EC lid angle is an approximation
	 * with uncalibrated accelerometers. The AP calculates a separate,
	 * more accurate lid angle.
	 */
#ifdef CONFIG_LID_ANGLE
	lpc_data[0] = motion_lid_get_angle();
#else
	lpc_data[0] = LID_ANGLE_UNRELIABLE;
#endif
	/* Assumptions on the list of sensors */
	for (i = 0; i < MIN(motion_sensor_count, 3); i++) {
		sensor = &motion_sensors[i];
		lpc_data[1+3*i] = sensor->xyz[X];
		lpc_data[2+3*i] = sensor->xyz[Y];
		lpc_data[3+3*i] = sensor->xyz[Z];
	}

	/*
	 * Increment sample id and clear busy bit to signal we finished
	 * updating data.
	 */
	*psample_id = (*psample_id + 1) &
			EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	*lpc_status = EC_MEMMAP_ACC_STATUS_PRESENCE_BIT | *psample_id;
}
#endif

static int motion_sense_read(struct motion_sensor_t *sensor)
{
	if (sensor->state != SENSOR_INITIALIZED)
		return EC_ERROR_UNKNOWN;

	if (sensor->drv->get_data_rate(sensor) == 0)
		return EC_ERROR_NOT_POWERED;

	/* Read all raw X,Y,Z accelerations. */
	return sensor->drv->read(sensor, sensor->raw_xyz);
}

static int motion_sense_process(struct motion_sensor_t *sensor,
				uint32_t *event,
				const timestamp_t *ts)
{
	int ret = EC_SUCCESS;

#ifdef CONFIG_ACCEL_INTERRUPTS
	if ((*event & TASK_EVENT_MOTION_INTERRUPT_MASK) &&
	    (sensor->drv->irq_handler != NULL)) {
		ret = sensor->drv->irq_handler(sensor, event);
		if (ret == EC_SUCCESS)
			sensor->last_collection = ts->le.lo;
	}
#endif
#ifdef CONFIG_ACCEL_FIFO
	if (sensor->drv->load_fifo != NULL) {
		/* Load fifo is filling raw_xyz sensor vector */
		sensor->drv->load_fifo(sensor);
	} else if (motion_sensor_time_to_read(ts, sensor)) {
		struct ec_response_motion_sensor_data vector;
		ret = motion_sense_read(sensor);
		if (ret == EC_SUCCESS) {
			vector.flags = 0;
			vector.sensor_num = sensor - motion_sensors;
			vector.data[X] = sensor->raw_xyz[X];
			vector.data[Y] = sensor->raw_xyz[Y];
			vector.data[Z] = sensor->raw_xyz[Z];
			motion_sense_fifo_add_unit(&vector, sensor, 3);
			sensor->last_collection = ts->le.lo;
		}
	} else {
		ret = EC_ERROR_BUSY;
	}
	if (*event & TASK_EVENT_MOTION_FLUSH_PENDING) {
		int flush_pending;
		flush_pending = atomic_read_clear(&sensor->flush_pending);
		for (; flush_pending > 0; flush_pending--) {
			fifo_flush_needed = 1;
			motion_sense_insert_flush(sensor);
		}
	}
#else
	if (motion_sensor_time_to_read(ts, sensor)) {
		/* Get latest data for local calculation */
		ret = motion_sense_read(sensor);
	} else {
		ret = EC_ERROR_BUSY;
	}
	if (ret == EC_SUCCESS) {
		sensor->last_collection = ts->le.lo;
		mutex_lock(&g_sensor_mutex);
		memcpy(sensor->xyz, sensor->raw_xyz, sizeof(sensor->xyz));
		mutex_unlock(&g_sensor_mutex);
	}

#endif
	return ret;
}

/*
 * Motion Sense Task
 * Requirement: motion_sensors[] are defined in board.c file.
 * Two (minimum) Accelerometers:
 *    1 in the A/B(lid, display) and 1 in the C/D(base, keyboard)
 * Gyro Sensor (optional)
 */
void motion_sense_task(void)
{
	int i, ret, wait_us;
	timestamp_t ts_begin_task, ts_end_task;
	uint32_t event = 0;
	uint16_t ready_status;
	struct motion_sensor_t *sensor;
#ifdef CONFIG_LID_ANGLE
	const uint16_t lid_angle_sensors = ((1 << CONFIG_LID_ANGLE_SENSOR_BASE)|
					    (1 << CONFIG_LID_ANGLE_SENSOR_LID));
#endif
#ifdef CONFIG_ACCEL_FIFO
	timestamp_t ts_last_int;
#endif
#ifdef UPDATE_HOST_MEM_MAP
	int sample_id = 0;
	uint8_t *lpc_status;
	uint16_t *lpc_data;

	lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	lpc_data = (uint16_t *)host_get_memmap(EC_MEMMAP_ACC_DATA);
	set_present(lpc_status);
#endif

#ifdef CONFIG_ACCEL_FIFO
	ts_last_int = get_time();
#endif
	while (1) {
		ts_begin_task = get_time();
		ready_status = 0;
		for (i = 0; i < motion_sensor_count; ++i) {

			sensor = &motion_sensors[i];

			/* if the sensor is active in the current power state */
			if (SENSOR_ACTIVE(sensor)) {
				if (sensor->state != SENSOR_INITIALIZED) {
					continue;
				}

				ret = motion_sense_process(sensor, &event,
						&ts_begin_task);
				if (ret != EC_SUCCESS)
					continue;
				ready_status |= (1 << i);
			}
		}

#ifdef CONFIG_GESTURE_DETECTION
#ifdef CONFIG_GESTURE_SW_DETECTION
		/* Run gesture recognition engine */
		gesture_calc(&event);
#endif
#ifdef CONFIG_GESTURE_SENSOR_BATTERY_TAP
		if (event & CONFIG_GESTURE_TAP_EVENT) {
#ifdef CONFIG_ACCEL_FIFO
			struct ec_response_motion_sensor_data vector;

			/*
			 * Send events to the FIFO
			 * AP is ignoring double tap event, do no wake up and no
			 * automatic disable.
			 */
			vector.flags = 0;
			vector.activity = MOTIONSENSE_ACTIVITY_DOUBLE_TAP;
			vector.state = 1; /* triggered */
			vector.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID;
			motion_sense_fifo_add_unit(&vector, NULL, 0);
#endif
			CPRINTS("double tap!");
			lightbar_sequence(LIGHTBAR_TAP);
		}
#endif
#ifdef CONFIG_GESTURE_SIGMO
		if (event & CONFIG_GESTURE_SIGMO_EVENT) {
			struct motion_sensor_t *activity_sensor;
#ifdef CONFIG_ACCEL_FIFO
			struct ec_response_motion_sensor_data vector;

			/* Send events to the FIFO */
			vector.flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP;
			vector.activity = MOTIONSENSE_ACTIVITY_SIG_MOTION;
			vector.state = 1; /* triggered */
			vector.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID;
			motion_sense_fifo_add_unit(&vector, NULL, 0);
#endif
			CPRINTS("significant motion");
			/* Disable further detection */
			activity_sensor = &motion_sensors[CONFIG_GESTURE_SIGMO];
			activity_sensor->drv->manage_activity(
					activity_sensor,
					MOTIONSENSE_ACTIVITY_SIG_MOTION,
					0, NULL);
		}
#endif
#endif
#ifdef CONFIG_LID_ANGLE
		/*
		 * Check to see that the sensors required for lid angle
		 * calculation are ready.
		 */
		ready_status &= lid_angle_sensors;
		if (ready_status == lid_angle_sensors)
			motion_lid_calc();
#endif
#ifdef CONFIG_CMD_ACCEL_INFO
		if (accel_disp) {
			CPRINTF("[%T event 0x%08x ", event);
			for (i = 0; i < motion_sensor_count; ++i) {
				sensor = &motion_sensors[i];
				CPRINTF("%s=%-5d, %-5d, %-5d ", sensor->name,
					sensor->xyz[X],
					sensor->xyz[Y],
					sensor->xyz[Z]);
			}
#ifdef CONFIG_LID_ANGLE
			CPRINTF("a=%-4d", motion_lid_get_angle());
#endif
			CPRINTF("]\n");
		}
#endif
#ifdef UPDATE_HOST_MEM_MAP
		update_sense_data(lpc_status, lpc_data, &sample_id);
#endif

		ts_end_task = get_time();
#ifdef CONFIG_ACCEL_FIFO
		/*
		 * Ask the host to flush the queue if
		 * - a flush event has been queued.
		 * - the queue is almost full,
		 * - we haven't done it for a while.
		 */
		if (fifo_flush_needed || wake_up_needed ||
		    event & TASK_EVENT_MOTION_ODR_CHANGE ||
		    queue_space(&motion_sense_fifo) < CONFIG_ACCEL_FIFO_THRES ||
		    (motion_int_interval > 0 &&
		     time_after(ts_end_task.le.lo,
				ts_last_int.le.lo + motion_int_interval))) {
			if (!fifo_flush_needed)
				motion_sense_insert_timestamp();
			fifo_flush_needed = 0;
			ts_last_int = ts_end_task;
			/*
			 * Count the number of event the AP is allowed to
			 * collect.
			 */
			mutex_lock(&g_sensor_mutex);
			fifo_queue_count = queue_count(&motion_sense_fifo);
			mutex_unlock(&g_sensor_mutex);
#ifdef CONFIG_MKBP_EVENT
			/*
			 * Send an event if we know we are in S0 and the kernel
			 * driver is listening, or the AP needs to be waken up.
			 * In the latter case, the driver pulls the event and
			 * will resume listening until it is suspended again.
			 */
			if ((fifo_int_enabled &&
			     sensor_active == SENSOR_ACTIVE_S0) ||
			    wake_up_needed) {
				mkbp_send_event(EC_MKBP_EVENT_SENSOR_FIFO);
				wake_up_needed = 0;
			}
#endif
		}
#endif
		if (motion_interval > 0) {
			/*
			 * Delay appropriately to keep sampling time
			 * consistent.
			 */
			wait_us = motion_interval -
				(ts_end_task.val - ts_begin_task.val);

			/* and it cannnot be negative */
			wait_us = MAX(wait_us, 0);

			/*
			 * Guarantee some minimum delay to allow other lower
			 * priority tasks to run.
			 */
			if (wait_us < motion_min_interval)
				wait_us = motion_min_interval;
		} else {
			wait_us = -1;
		}

		event = task_wait_event(wait_us);
	}
}

#ifdef CONFIG_ACCEL_FIFO
static int motion_sense_get_next_event(uint8_t *out)
{
	union ec_response_get_next_data *data =
		(union ec_response_get_next_data *)out;
	/* out is not padded. It has one byte for the event type */
	motion_sense_get_fifo_info(&data->sensor_fifo.info);
	return sizeof(data->sensor_fifo);
}

DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SENSOR_FIFO, motion_sense_get_next_event);
#endif
/*****************************************************************************/
/* Host commands */

/* Function to map host sensor IDs to motion sensor. */
static struct motion_sensor_t
	*host_sensor_id_to_real_sensor(int host_id)
{
	struct motion_sensor_t *sensor;

	if (host_id >= motion_sensor_count)
		return NULL;
	sensor = &motion_sensors[host_id];

	/* if sensor is powered and initialized, return match */
	if (SENSOR_ACTIVE(sensor) && (sensor->state == SENSOR_INITIALIZED))
		return sensor;

	/* If no match then the EC currently doesn't support ID received. */
	return NULL;
}

static struct motion_sensor_t
	*host_sensor_id_to_motion_sensor(int host_id)
{
#ifdef CONFIG_GESTURE_HOST_DETECTION
	if (host_id == MOTION_SENSE_ACTIVITY_SENSOR_ID)
		/*
		 * Return the info for the first sensor that
		 * support some gestures.
		 */
		return host_sensor_id_to_real_sensor(
			__builtin_ctz(CONFIG_GESTURE_DETECTION_MASK));
#endif
	return host_sensor_id_to_real_sensor(host_id);
}

static int host_cmd_motion_sense(struct host_cmd_handler_args *args)
{
	const struct ec_params_motion_sense *in = args->params;
	struct ec_response_motion_sense *out = args->response;
	struct motion_sensor_t *sensor;
	int i, ret = EC_RES_INVALID_PARAM, reported;

	switch (in->cmd) {
	case MOTIONSENSE_CMD_DUMP:
		out->dump.module_flags =
			(*(host_get_memmap(EC_MEMMAP_ACC_STATUS)) &
			 EC_MEMMAP_ACC_STATUS_PRESENCE_BIT) ?
			MOTIONSENSE_MODULE_FLAG_ACTIVE : 0;
		out->dump.sensor_count = ALL_MOTION_SENSORS;
		args->response_size = sizeof(out->dump);
		reported = MIN(ALL_MOTION_SENSORS, in->dump.max_sensor_count);
		mutex_lock(&g_sensor_mutex);
		for (i = 0; i < reported; i++) {
			out->dump.sensor[i].flags =
				MOTIONSENSE_SENSOR_FLAG_PRESENT;
			if (i < motion_sensor_count) {
				sensor = &motion_sensors[i];
				/* casting from int to s16 */
				out->dump.sensor[i].data[X] = sensor->xyz[X];
				out->dump.sensor[i].data[Y] = sensor->xyz[Y];
				out->dump.sensor[i].data[Z] = sensor->xyz[Z];
			} else {
				memset(out->dump.sensor[i].data, 0,
				       3 * sizeof(int16_t));
			}
		}
		mutex_unlock(&g_sensor_mutex);
		args->response_size += reported *
			sizeof(struct ec_response_motion_sensor_data);
		break;

	case MOTIONSENSE_CMD_DATA:
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		out->data.flags = 0;

		mutex_lock(&g_sensor_mutex);
		out->data.data[X] = sensor->xyz[X];
		out->data.data[Y] = sensor->xyz[Y];
		out->data.data[Z] = sensor->xyz[Z];
		mutex_unlock(&g_sensor_mutex);
		args->response_size = sizeof(out->data);
		break;

	case MOTIONSENSE_CMD_INFO:
		sensor = host_sensor_id_to_motion_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

#ifdef CONFIG_GESTURE_HOST_DETECTION
		if (in->sensor_odr.sensor_num ==
		    MOTION_SENSE_ACTIVITY_SENSOR_ID)
			out->info.type = MOTIONSENSE_TYPE_ACTIVITY;
		else
#endif
			out->info.type = sensor->type;
		out->info.location = sensor->location;
		out->info.chip = sensor->chip;

		args->response_size = sizeof(out->info);
		break;

	case MOTIONSENSE_CMD_EC_RATE:
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/*
		 * Set new sensor sampling rate when AP is on, if the data arg
		 * has a value.
		 */
		if (in->ec_rate.data != EC_MOTION_SENSE_NO_VALUE) {
			sensor->config[SENSOR_CONFIG_AP].ec_rate =
				motion_sense_set_ec_rate_from_ap(
					sensor, in->ec_rate.data * MSEC);
			/* Bound the new sampling rate. */
			motion_sense_set_motion_intervals();
		}

		out->ec_rate.ret = motion_sense_ec_rate(sensor) / MSEC;

		args->response_size = sizeof(out->ec_rate);
		break;

	case MOTIONSENSE_CMD_SENSOR_ODR:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new data rate if the data arg has a value. */
		if (in->sensor_odr.data != EC_MOTION_SENSE_NO_VALUE) {
#ifdef CONFIG_ACCEL_FIFO
			/*
			 * To be sure timestamps are calculated properly,
			 * Send an event to have a timestamp inserted in the
			 * FIFO.
			 */
			motion_sense_insert_timestamp();
#endif
			sensor->config[SENSOR_CONFIG_AP].odr =
				in->sensor_odr.data |
				(in->sensor_odr.roundup ? ROUND_UP_FLAG : 0);

			ret = motion_sense_set_data_rate(sensor);
			if (ret != EC_SUCCESS)
				return EC_RES_INVALID_PARAM;

#ifdef CONFIG_ACCEL_FIFO
			/*
			 * The new ODR may suspend sensor, leaving samples
			 * in the FIFO. Flush it explicitly.
			 */
			task_set_event(TASK_ID_MOTIONSENSE,
					TASK_EVENT_MOTION_ODR_CHANGE, 0);
#endif
			/*
			 * If the sensor was suspended before, or now
			 * suspended, we have to recalculate the EC sampling
			 * rate
			 */
			motion_sense_set_motion_intervals();
		}

		out->sensor_odr.ret = sensor->drv->get_data_rate(sensor);

		args->response_size = sizeof(out->sensor_odr);

		break;

	case MOTIONSENSE_CMD_SENSOR_RANGE:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_range.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new range if the data arg has a value. */
		if (in->sensor_range.data != EC_MOTION_SENSE_NO_VALUE) {
			if (sensor->drv->set_range(sensor,
						in->sensor_range.data,
						in->sensor_range.roundup)
					!= EC_SUCCESS) {
				return EC_RES_INVALID_PARAM;
			}
		}

		out->sensor_range.ret = sensor->drv->get_range(sensor);
		args->response_size = sizeof(out->sensor_range);
		break;

	case MOTIONSENSE_CMD_SENSOR_OFFSET:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_offset.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		/* Set new range if the data arg has a value. */
		if (in->sensor_offset.flags & MOTION_SENSE_SET_OFFSET) {
			ret = sensor->drv->set_offset(sensor,
						in->sensor_offset.offset,
						in->sensor_offset.temp);
			if (ret != EC_SUCCESS)
				return ret;
		}

		ret = sensor->drv->get_offset(sensor, out->sensor_offset.offset,
				&out->sensor_offset.temp);
		if (ret != EC_SUCCESS)
			return ret;
		args->response_size = sizeof(out->sensor_offset);
		break;

	case MOTIONSENSE_CMD_PERFORM_CALIB:
		/* Verify sensor number is valid. */
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_offset.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;
		if (!sensor->drv->perform_calib)
			return EC_RES_INVALID_COMMAND;

		ret = sensor->drv->perform_calib(sensor);
		if (ret != EC_SUCCESS)
			return ret;
		ret = sensor->drv->get_offset(sensor, out->sensor_offset.offset,
				&out->sensor_offset.temp);
		if (ret != EC_SUCCESS)
			return ret;
		args->response_size = sizeof(out->sensor_offset);
		break;

#ifdef CONFIG_ACCEL_FIFO
	case MOTIONSENSE_CMD_FIFO_FLUSH:
		sensor = host_sensor_id_to_real_sensor(
				in->sensor_odr.sensor_num);
		if (sensor == NULL)
			return EC_RES_INVALID_PARAM;

		atomic_add(&sensor->flush_pending, 1);

		task_set_event(TASK_ID_MOTIONSENSE,
			       TASK_EVENT_MOTION_FLUSH_PENDING, 0);
		/* pass-through */
	case MOTIONSENSE_CMD_FIFO_INFO:
		motion_sense_get_fifo_info(&out->fifo_info);
		for (i = 0; i < motion_sensor_count; i++) {
			out->fifo_info.lost[i] = motion_sensors[i].lost;
			motion_sensors[i].lost = 0;
		}
		motion_sense_fifo_lost = 0;
		args->response_size = sizeof(out->fifo_info) +
			sizeof(uint16_t) * motion_sensor_count;
		break;

	case MOTIONSENSE_CMD_FIFO_READ:
		mutex_lock(&g_sensor_mutex);
		reported = MIN((args->response_max - sizeof(out->fifo_read)) /
			       motion_sense_fifo.unit_bytes,
			       MIN(queue_count(&motion_sense_fifo),
				   in->fifo_read.max_data_vector));
		reported = queue_remove_units(&motion_sense_fifo,
				out->fifo_read.data, reported);
		mutex_unlock(&g_sensor_mutex);
		out->fifo_read.number_data = reported;
		args->response_size = sizeof(out->fifo_read) + reported *
			motion_sense_fifo.unit_bytes;
		break;
	case MOTIONSENSE_CMD_FIFO_INT_ENABLE:
		switch (in->fifo_int_enable.enable) {
		case 0:
		case 1:
			fifo_int_enabled = in->fifo_int_enable.enable;
		case EC_MOTION_SENSE_NO_VALUE:
			out->fifo_int_enable.ret = fifo_int_enabled;
			args->response_size = sizeof(out->fifo_int_enable);
			break;
		default:
			return EC_RES_INVALID_PARAM;
		}
		break;
#else
	case MOTIONSENSE_CMD_FIFO_INFO:
		/* Only support the INFO command, to tell there is no FIFO. */
		memset(&out->fifo_info, 0, sizeof(out->fifo_info));
		args->response_size = sizeof(out->fifo_info);
		break;
#endif
#ifdef CONFIG_GESTURE_HOST_DETECTION
	case MOTIONSENSE_CMD_LIST_ACTIVITIES: {
		uint32_t enabled, disabled, mask, i;

		out->list_activities.enabled = 0;
		out->list_activities.disabled = 0;
		ret = EC_RES_SUCCESS;
		mask = CONFIG_GESTURE_DETECTION_MASK;
		while (mask && ret == EC_RES_SUCCESS) {
			i = get_next_bit(&mask);
			sensor = &motion_sensors[i];
			ret = sensor->drv->list_activities(sensor,
					&enabled, &disabled);
			if (ret == EC_RES_SUCCESS) {
				out->list_activities.enabled |= enabled;
				out->list_activities.disabled |= disabled;
			}
		}
		if (ret != EC_RES_SUCCESS)
			return ret;
		args->response_size = sizeof(out->list_activities);
		break;
	}
	case MOTIONSENSE_CMD_SET_ACTIVITY: {
		uint32_t enabled, disabled, mask, i;

		mask = CONFIG_GESTURE_DETECTION_MASK;
		ret = EC_RES_SUCCESS;
		while (mask && ret == EC_RES_SUCCESS) {
			i = get_next_bit(&mask);
			sensor = &motion_sensors[i];
			sensor->drv->list_activities(sensor,
					&enabled, &disabled);
			if ((1 << in->set_activity.activity) &
			    (enabled | disabled))
				ret = sensor->drv->manage_activity(sensor,
						in->set_activity.activity,
						in->set_activity.enable,
						&in->set_activity);
		}
		if (ret != EC_RES_SUCCESS)
			return ret;
		args->response_size = sizeof(out->set_activity);
		break;
	}
#endif
	default:
		/* Call other users of the motion task */
#ifdef CONFIG_LID_ANGLE
		if (ret == EC_RES_INVALID_PARAM)
			ret = host_cmd_motion_lid(args);
#endif
		return ret;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_MOTION_SENSE_CMD,
		     host_cmd_motion_sense,
		     EC_VER_MASK(1) | EC_VER_MASK(2));

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_ACCELS
static int command_accelrange(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;
	struct motion_sensor_t *sensor;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Write new range, if it returns invalid arg, then return
		 * a parameter error.
		 */
		if (sensor->drv->set_range(sensor,
					   data,
					   round) == EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		ccprintf("Range for sensor %d: %d\n", id,
			 sensor->drv->get_range(sensor));
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrange, command_accelrange,
	"id [data [roundup]]",
	"Read or write accelerometer range", NULL);

static int command_accelresolution(int argc, char **argv)
{
	char *e;
	int id, data, round = 1;
	struct motion_sensor_t *sensor;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Write new resolution, if it returns invalid arg, then
		 * return a parameter error.
		 */
		if (sensor->drv->set_resolution(sensor, data, round)
			== EC_ERROR_INVAL)
			return EC_ERROR_PARAM2;
	} else {
		ccprintf("Resolution for sensor %d: %d\n", id,
			 sensor->drv->get_resolution(sensor));
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelres, command_accelresolution,
	"id [data [roundup]]",
	"Read or write accelerometer resolution", NULL);

static int command_accel_data_rate(int argc, char **argv)
{
	char *e;
	int id, data, round = 1, ret;
	struct motion_sensor_t *sensor;
	enum sensor_config config_id;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	if (argc >= 3) {
		/* Second argument is data to write. */
		data = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (argc == 4) {
			/* Third argument is rounding flag. */
			round = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
		}

		/*
		 * Take ownership of the sensor and
		 * Write new data rate, if it returns invalid arg, then
		 * return a parameter error.
		 */
		config_id = motion_sense_get_ec_config();
		sensor->config[SENSOR_CONFIG_AP].odr = 0;
		sensor->config[config_id].odr =
			data | (round ? ROUND_UP_FLAG : 0);
		ret = motion_sense_set_data_rate(sensor);
		if (ret)
			return EC_ERROR_PARAM2;
		/* Sensor might be out of suspend, check the ec_rate */
		motion_sense_set_motion_intervals();
	} else {
		ccprintf("Data rate for sensor %d: %d\n", id,
			 sensor->drv->get_data_rate(sensor));
		ccprintf("EC rate for sensor %d: %d\n", id,
			 motion_sense_ec_rate(sensor));
		ccprintf("Current EC rate: %d\n", motion_interval);
		ccprintf("Current Interrupt rate: %d\n", motion_int_interval);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelrate, command_accel_data_rate,
	"id [data [roundup]]",
	"Read or write accelerometer ODR", NULL);

static int command_accel_read_xyz(int argc, char **argv)
{
	char *e;
	int id, n = 1, ret;
	struct motion_sensor_t *sensor;
	vector_3_t v;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);

	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	if (argc >= 3)
		n = strtoi(argv[2], &e, 0);

	sensor = &motion_sensors[id];

	while ((n == -1) || (n-- > 0)) {
		ret = sensor->drv->read(sensor, v);
		if (ret == 0)
			ccprintf("Current data %d: %-5d %-5d %-5d\n",
				 id, v[X], v[Y], v[Z]);
		else
			ccprintf("vector not ready\n");
		ccprintf("Last calib. data %d: %-5d %-5d %-5d\n",
			 id, sensor->xyz[X], sensor->xyz[Y], sensor->xyz[Z]);
		task_wait_event(motion_min_interval);
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(accelread, command_accel_read_xyz,
	"id [n]",
	"Read sensor x/y/z", NULL);

static int command_accel_init(int argc, char **argv)
{
	char *e;
	int id, ret;
	struct motion_sensor_t *sensor;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is sensor id. */
	id = strtoi(argv[1], &e, 0);

	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];
	ret = motion_sense_init(sensor);

	ccprintf("%s: state %d - %d\n", sensor->name, sensor->state, ret);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelinit, command_accel_init,
	"id",
	"Init sensor", NULL);

#ifdef CONFIG_CMD_ACCEL_INFO
static int command_display_accel_info(int argc, char **argv)
{
	char *e;
	int val;

	if (argc > 3)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is on/off whether to display accel data. */
	if (argc > 1) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		accel_disp = val;
	}

	/*
	 * Second arg changes the accel task time interval. Note accel
	 * sampling interval will be clobbered when chipset suspends or
	 * resumes.
	 */
	if (argc > 2) {
		val = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		motion_interval = val * MSEC;
		task_wake(TASK_ID_MOTIONSENSE);

	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelinfo, command_display_accel_info,
	"on/off [interval]",
	"Print motion sensor info, lid angle calculations"
	" and set calculation frequency.", NULL);
#endif /* CONFIG_CMD_ACCEL_INFO */

#ifdef CONFIG_ACCEL_INTERRUPTS
/* TODO(crosbug.com/p/426659): this code is broken, does not with ST sensors. */
void accel_int_lid(enum gpio_signal signal)
{
	/*
	 * Print statement is here for testing with console accelint command.
	 * Remove print statement when interrupt is used for real.
	 */
	CPRINTS("Accelerometer wake-up interrupt occurred on lid");
}

void accel_int_base(enum gpio_signal signal)
{
	/*
	 * Print statement is here for testing with console accelint command.
	 * Remove print statement when interrupt is used for real.
	 */
	CPRINTS("Accelerometer wake-up interrupt occurred on base");
}

static int command_accelerometer_interrupt(int argc, char **argv)
{
	char *e;
	int id, thresh;
	struct motion_sensor_t *sensor;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	/* First argument is id. */
	id = strtoi(argv[1], &e, 0);
	if (*e || id < 0 || id >= motion_sensor_count)
		return EC_ERROR_PARAM1;

	sensor = &motion_sensors[id];

	/* Second argument is interrupt threshold. */
	thresh = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	sensor->drv->set_interrupt(sensor, thresh);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelint, command_accelerometer_interrupt,
	"id threshold",
	"Write interrupt threshold", NULL);
#endif /* CONFIG_ACCEL_INTERRUPTS */

#ifdef CONFIG_ACCEL_FIFO
static int motion_sense_read_fifo(int argc, char **argv)
{
	int count, i;
	struct ec_response_motion_sensor_data v;

	if (argc < 1)
		return EC_ERROR_PARAM_COUNT;

	/* Limit the amount of data to avoid saturating the UART buffer */
	count = MIN(queue_count(&motion_sense_fifo), 16);
	for (i = 0; i < count; i++) {
		queue_peek_units(&motion_sense_fifo, &v, i, 1);
		if (v.flags & (MOTIONSENSE_SENSOR_FLAG_TIMESTAMP |
			       MOTIONSENSE_SENSOR_FLAG_FLUSH)) {
			uint64_t timestamp;
			memcpy(&timestamp, v.data, sizeof(v.data));
			ccprintf("Timestamp: 0x%016lx%s\n", timestamp,
				 (v.flags & MOTIONSENSE_SENSOR_FLAG_FLUSH ?
				  " - Flush" : ""));
		} else {
			ccprintf("%d %d: %-5d %-5d %-5d\n", i, v.sensor_num,
				 v.data[X], v.data[Y], v.data[Z]);
		}
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(fiforead, motion_sense_read_fifo,
	"id",
	"Read Fifo sensor", NULL);
#endif

#endif /* CONFIG_CMD_ACCELS */
