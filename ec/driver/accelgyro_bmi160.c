/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * BMI160 accelerometer and gyro module for Chrome EC
 * 3D digital accelerometer & 3D digital gyroscope
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/mag_bmm150.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ## args)

/*
 * Struct for pairing an engineering value with the register value for a
 * parameter.
 */
struct accel_param_pair {
	int val; /* Value in engineering units. */
	int reg_val; /* Corresponding register value. */
};

/* List of range values in +/-G's and their associated register values. */
static const struct accel_param_pair g_ranges[] = {
	{2, BMI160_GSEL_2G},
	{4, BMI160_GSEL_4G},
	{8, BMI160_GSEL_8G},
	{16, BMI160_GSEL_16G}
};

/*
 * List of angular rate range values in +/-dps's
 * and their associated register values.
 */
const struct accel_param_pair dps_ranges[] = {
	{125, BMI160_DPS_SEL_125},
	{250, BMI160_DPS_SEL_250},
	{500, BMI160_DPS_SEL_500},
	{1000, BMI160_DPS_SEL_1000},
	{2000, BMI160_DPS_SEL_2000}
};

static int wakeup_time[] = {
	[MOTIONSENSE_TYPE_ACCEL] = 4,
	[MOTIONSENSE_TYPE_GYRO] = 80,
	[MOTIONSENSE_TYPE_MAG] = 1
};

static inline const struct accel_param_pair *get_range_table(
		enum motionsensor_type type, int *psize)
{
	if (MOTIONSENSE_TYPE_ACCEL == type) {
		if (psize)
			*psize = ARRAY_SIZE(g_ranges);
		return g_ranges;
	} else {
		if (psize)
			*psize = ARRAY_SIZE(dps_ranges);
		return dps_ranges;
	}
}

static inline int get_xyz_reg(enum motionsensor_type type)
{
	switch (type) {
	case MOTIONSENSE_TYPE_ACCEL:
		return BMI160_ACC_X_L_G;
	case MOTIONSENSE_TYPE_GYRO:
		return BMI160_GYR_X_L_G;
	case MOTIONSENSE_TYPE_MAG:
		return BMI160_MAG_X_L_G;
	default:
		return -1;
	}
}

/**
 * @return reg value that matches the given engineering value passed in.
 * The round_up flag is used to specify whether to round up or down.
 * Note, this function always returns a valid reg value. If the request is
 * outside the range of values, it returns the closest valid reg value.
 */
static int get_reg_val(const int eng_val, const int round_up,
		const struct accel_param_pair *pairs, const int size)
{
	int i;

	for (i = 0; i < size - 1; i++) {
		if (eng_val <= pairs[i].val)
			break;

		if (eng_val < pairs[i+1].val) {
			if (round_up)
				i += 1;
			break;
		}
	}
	return pairs[i].reg_val;
}

/**
 * @return engineering value that matches the given reg val
 */
static int get_engineering_val(const int reg_val,
		const struct accel_param_pair *pairs, const int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (reg_val == pairs[i].reg_val)
			break;
	}
	return pairs[i].val;
}

#ifdef CONFIG_SPI_ACCEL_PORT
static inline int spi_raw_read(const int addr, const uint8_t reg,
			       uint8_t *data, const int len)
{
	uint8_t cmd = 0x80 | reg;

	return spi_transaction(&spi_devices[addr], &cmd, 1, data, len);
}
#endif
/**
 * Read 8bit register from accelerometer.
 */
static int raw_read8(const int port, const int addr, const uint8_t reg,
					 int *data_ptr)
{
	int rv = -EC_ERROR_PARAM1;

	if (BMI160_IS_SPI(addr)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t val;
		rv = spi_raw_read(BMI160_SPI_ADDRESS(addr), reg, &val, 1);
		if (rv == EC_SUCCESS)
			*data_ptr = val;
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		rv = i2c_read8(port, BMI160_I2C_ADDRESS(addr),
			       reg, data_ptr);
#endif
	}
	return rv;
}

/**
 * Write 8bit register from accelerometer.
 */
static int raw_write8(const int port, const int addr, const uint8_t reg,
					  int data)
{
	int rv = -EC_ERROR_PARAM1;

	if (BMI160_IS_SPI(addr)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		uint8_t cmd[2] = { reg, data };
		rv = spi_transaction(&spi_devices[BMI160_SPI_ADDRESS(addr)],
				     cmd, 2, NULL, 0);
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		rv = i2c_write8(port, BMI160_I2C_ADDRESS(addr),
				reg, data);
#endif
	}
	/*
	 * From Bosch:  BMI160 needs a delay of 450us after each write if it
	 * is in suspend mode, otherwise the operation may be ignored by
	 * the sensor. Given we are only doing write during init, add
	 * the delay inconditionally.
	 */
	msleep(1);
	return rv;
}

#ifdef CONFIG_ACCEL_INTERRUPTS
/**
 * Read 32bit register from accelerometer.
 */
static int raw_read32(const int port, const int addr, const uint8_t reg,
					  int *data_ptr)
{
	int rv = -EC_ERROR_PARAM1;
	if (BMI160_IS_SPI(addr)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		rv = spi_raw_read(BMI160_SPI_ADDRESS(addr), reg,
				  (uint8_t *)data_ptr, 4);
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		rv = i2c_read32(port, BMI160_I2C_ADDRESS(addr),
				reg, data_ptr);
#endif
	}
	return rv;
}
#endif /* defined(CONFIG_ACCEL_INTERRUPTS) */

/**
 * Read n bytes from accelerometer.
 */
static int raw_read_n(const int port, const int addr, const uint8_t reg,
		uint8_t *data_ptr, const int len)
{
	int rv = -EC_ERROR_PARAM1;

	if (BMI160_IS_SPI(addr)) {
#ifdef CONFIG_SPI_ACCEL_PORT
		rv = spi_raw_read(BMI160_SPI_ADDRESS(addr), reg, data_ptr, len);
#endif
	} else {
#ifdef I2C_PORT_ACCEL
		i2c_lock(port, 1);
		rv = i2c_xfer(port, BMI160_I2C_ADDRESS(addr), &reg, 1,
				data_ptr, len, I2C_XFER_SINGLE);
		i2c_lock(port, 0);
#endif
	}
	return rv;
}

#ifdef CONFIG_MAG_BMI160_BMM150
/**
 * Control access to the compass on the secondary i2c interface:
 * enable values are:
 * 1: manual access, we can issue i2c to the compass
 * 0: data access: BMI160 gather data periodically from the compass.
 */
static int bmm150_mag_access_ctrl(const int port, const int addr,
				  const int enable)
{
	int mag_if_ctrl;
	raw_read8(port, addr, BMI160_MAG_IF_1, &mag_if_ctrl);
	if (enable) {
		mag_if_ctrl |= BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_1;
	} else {
		mag_if_ctrl &= ~BMI160_MAG_MANUAL_EN;
		mag_if_ctrl &= ~BMI160_MAG_READ_BURST_MASK;
		mag_if_ctrl |= BMI160_MAG_READ_BURST_8;
	}
	return raw_write8(port, addr, BMI160_MAG_IF_1, mag_if_ctrl);
}

/**
 * Read register from compass.
 * Assuming we are in manual access mode, read compass i2c register.
 */
int raw_mag_read8(const int port, const int addr, const uint8_t reg,
				  int *data_ptr)
{
	/* Only read 1 bytes */
	raw_write8(port, addr, BMI160_MAG_I2C_READ_ADDR, reg);
	return raw_read8(port, addr, BMI160_MAG_I2C_READ_DATA, data_ptr);
}

/**
 * Write register from compass.
 * Assuming we are in manual access mode, write to compass i2c register.
 */
int raw_mag_write8(const int port, const int addr, const uint8_t reg, int data)
{
	raw_write8(port, addr, BMI160_MAG_I2C_WRITE_DATA, data);
	return raw_write8(port, addr, BMI160_MAG_I2C_WRITE_ADDR, reg);
}
#endif

#ifdef CONFIG_ACCEL_FIFO
static int enable_fifo(const struct motion_sensor_t *s, int enable)
{
	struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);
	int ret, val;

	if (enable) {
		/* FIFO start collecting events */
		ret = raw_read8(s->port, s->addr, BMI160_FIFO_CONFIG_1, &val);
		val |= BMI160_FIFO_SENSOR_EN(s->type);
		ret = raw_write8(s->port, s->addr, BMI160_FIFO_CONFIG_1, val);
		if (ret == EC_SUCCESS)
			data->flags |= 1 << (s->type + BMI160_FIFO_FLAG_OFFSET);

	} else {
		/* FIFO stop collecting events */
		ret = raw_read8(s->port, s->addr, BMI160_FIFO_CONFIG_1, &val);
		val &= ~BMI160_FIFO_SENSOR_EN(s->type);
		ret = raw_write8(s->port, s->addr, BMI160_FIFO_CONFIG_1, val);
		if (ret == EC_SUCCESS)
			data->flags &=
				~(1 << (s->type + BMI160_FIFO_FLAG_OFFSET));
	}
	return ret;
}
#endif

static int set_range(const struct motion_sensor_t *s,
				int range,
				int rnd)
{
	int ret, range_tbl_size;
	uint8_t reg_val, ctrl_reg;
	const struct accel_param_pair *ranges;
	struct accelgyro_saved_data_t *data = BMI160_GET_SAVED_DATA(s);

	if (s->type == MOTIONSENSE_TYPE_MAG) {
		data->range = range;
		return EC_SUCCESS;
	}

	ctrl_reg = BMI160_RANGE_REG(s->type);
	ranges = get_range_table(s->type, &range_tbl_size);
	reg_val = get_reg_val(range, rnd, ranges, range_tbl_size);

	ret = raw_write8(s->port, s->addr, ctrl_reg, reg_val);
	/* Now that we have set the range, update the driver's value. */
	if (ret == EC_SUCCESS)
		data->range = get_engineering_val(reg_val, ranges,
				range_tbl_size);
	return ret;
}

static int get_range(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = BMI160_GET_SAVED_DATA(s);

	return data->range;
}

static int set_resolution(const struct motion_sensor_t *s,
				int res,
				int rnd)
{
	/* Only one resolution, BMI160_RESOLUTION, so nothing to do. */
	return EC_SUCCESS;
}

static int get_resolution(const struct motion_sensor_t *s)
{
	return BMI160_RESOLUTION;
}

static int set_data_rate(const struct motion_sensor_t *s,
				int rate,
				int rnd)
{
	int ret, val, normalized_rate;
	uint8_t ctrl_reg, reg_val;
	struct accelgyro_saved_data_t *data = BMI160_GET_SAVED_DATA(s);
#ifdef CONFIG_MAG_BMI160_BMM150
	struct mag_cal_t              *moc = BMM150_CAL(s);
#endif

	if (rate == 0) {
#ifdef CONFIG_ACCEL_FIFO
		/* FIFO stop collecting events */
		enable_fifo(s, 0);
#endif
		/* go to suspend mode */
		ret = raw_write8(s->port, s->addr, BMI160_CMD_REG,
				 BMI160_CMD_MODE_SUSPEND(s->type));
		msleep(3);
		data->odr = 0;
#ifdef CONFIG_MAG_BMI160_BMM150
		if (s->type == MOTIONSENSE_TYPE_MAG)
			moc->batch_size = 0;
#endif
		return ret;
	} else if (data->odr == 0) {
		/* back from suspend mode. */
		ret = raw_write8(s->port, s->addr, BMI160_CMD_REG,
				 BMI160_CMD_MODE_NORMAL(s->type));
		msleep(wakeup_time[s->type]);
	}
	ctrl_reg = BMI160_CONF_REG(s->type);
	reg_val = BMI160_ODR_TO_REG(rate);
	normalized_rate = BMI160_REG_TO_ODR(reg_val);
	if (rnd && (normalized_rate < rate)) {
		reg_val++;
		normalized_rate *= 2;
	}

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		if (reg_val > BMI160_ODR_1600HZ) {
			reg_val = BMI160_ODR_1600HZ;
			normalized_rate = 1600000;
		} else if (reg_val < BMI160_ODR_0_78HZ) {
			reg_val = BMI160_ODR_0_78HZ;
			normalized_rate = 780;
		}
		break;
	case MOTIONSENSE_TYPE_GYRO:
		if (reg_val > BMI160_ODR_3200HZ) {
			reg_val = BMI160_ODR_3200HZ;
			normalized_rate = 3200000;
		} else if (reg_val < BMI160_ODR_25HZ) {
			reg_val = BMI160_ODR_25HZ;
			normalized_rate = 25000;
		}
		break;
	case MOTIONSENSE_TYPE_MAG:
		/* We use the regular preset we can go about 100Hz */
		if (reg_val > BMI160_ODR_100HZ) {
			reg_val = BMI160_ODR_100HZ;
			normalized_rate = 100000;
		} else if (reg_val < BMI160_ODR_0_78HZ) {
			reg_val = BMI160_ODR_0_78HZ;
			normalized_rate = 780;
		}
		break;

	default:
		return -1;
	}

	/*
	 * Lock accel resource to prevent another task from attempting
	 * to write accel parameters until we are done.
	 */
	mutex_lock(s->mutex);

	ret = raw_read8(s->port, s->addr, ctrl_reg, &val);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	val = (val & ~BMI160_ODR_MASK) | reg_val;
	ret = raw_write8(s->port, s->addr, ctrl_reg, val);
	if (ret != EC_SUCCESS)
		goto accel_cleanup;

	/* Now that we have set the odr, update the driver's value. */
	data->odr = normalized_rate;

#ifdef CONFIG_MAG_BMI160_BMM150
	if (s->type == MOTIONSENSE_TYPE_MAG) {
		/* Reset the calibration */
		init_mag_cal(moc);
		/*
		 * We need at least MIN_BATCH_SIZE amd we must have collected
		 * for at least MIN_BATCH_WINDOW_US.
		 * Given odr is in mHz, multiply by 1000x
		 */
		moc->batch_size = MAX(
			MAG_CAL_MIN_BATCH_SIZE,
			(data->odr * 1000) / (MAG_CAL_MIN_BATCH_WINDOW_US));
		CPRINTS("Batch size: %d", moc->batch_size);
	}
#endif

#ifdef CONFIG_ACCEL_FIFO
	/*
	 * FIFO start collecting events.
	 * They will be discarded if AP does not want them.
	 */
	enable_fifo(s, 1);
#endif

accel_cleanup:
	mutex_unlock(s->mutex);
	return ret;
}

static int get_data_rate(const struct motion_sensor_t *s)
{
	struct accelgyro_saved_data_t *data = BMI160_GET_SAVED_DATA(s);

	return data->odr;
}
static int get_offset(const struct motion_sensor_t *s,
			int16_t    *offset,
			int16_t    *temp)
{
	int i, val, val98;
	vector_3_t v;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/*
		 * The offset of the accelerometer off_acc_[xyz] is a 8 bit
		 * two-complement number in units of 3.9 mg independent of the
		 * range selected for the accelerometer.
		 */
		for (i = X; i <= Z; i++) {
			raw_read8(s->port, s->addr, BMI160_OFFSET_ACC70 + i,
				  &val);
			if (val > 0x7f)
				val = -256 + val;
			v[i] = val * BMI160_OFFSET_ACC_MULTI_MG /
				BMI160_OFFSET_ACC_DIV_MG;
		}
		break;
	case MOTIONSENSE_TYPE_GYRO:
		/* Read the MSB first */
		raw_read8(s->port, s->addr, BMI160_OFFSET_EN_GYR98, &val98);
		/*
		 * The offset of the gyroscope off_gyr_[xyz] is a 10 bit
		 * two-complement number in units of 0.061 °/s.
		 * Therefore a maximum range that can be compensated is
		 * -31.25 °/s to +31.25 °/s
		 */
		for (i = X; i <= Z; i++) {
			raw_read8(s->port, s->addr, BMI160_OFFSET_GYR70 + i,
				  &val);
			val |= ((val98 >> (2 * i)) & 0x3) << 8;
			if (val > 0x1ff)
				val = -1024 + val;
			v[i] = val * BMI160_OFFSET_GYRO_MULTI_MDS /
				BMI160_OFFSET_GYRO_DIV_MDS;
		}
		break;
#ifdef CONFIG_MAG_BMI160_BMM150
	case MOTIONSENSE_TYPE_MAG:
		bmm150_get_offset(s, v);
		break;
#endif /* defined(CONFIG_MAG_BMI160_BMM150) */
	default:
		for (i = X; i <= Z; i++)
			v[i] = 0;
	}
	rotate(v, *s->rot_standard_ref, v);
	offset[X] = v[X];
	offset[Y] = v[Y];
	offset[Z] = v[Z];
	/* Saving temperature at calibration not supported yet */
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int set_offset(const struct motion_sensor_t *s,
			const int16_t *offset,
			int16_t    temp)
{
	int ret, i, val, val98;
	vector_3_t v = { offset[X], offset[Y], offset[Z] };

	rotate_inv(v, *s->rot_standard_ref, v);

	ret = raw_read8(s->port, s->addr, BMI160_OFFSET_EN_GYR98, &val98);
	if (ret != 0)
		return ret;

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		for (i = X; i <= Z; i++) {
			val = v[i] * BMI160_OFFSET_ACC_DIV_MG /
				BMI160_OFFSET_ACC_MULTI_MG;
			if (val > 127)
				val = 127;
			if (val < -128)
				val = -128;
			if (val < 0)
				val = 256 + val;
			raw_write8(s->port, s->addr, BMI160_OFFSET_ACC70 + i,
				   val);
		}
		ret = raw_write8(s->port, s->addr, BMI160_OFFSET_EN_GYR98,
				 val98 | BMI160_OFFSET_ACC_EN);
		break;
	case MOTIONSENSE_TYPE_GYRO:
		for (i = X; i <= Z; i++) {
			val = v[i] * BMI160_OFFSET_GYRO_DIV_MDS /
				BMI160_OFFSET_GYRO_MULTI_MDS;
			if (val > 511)
				val = 511;
			if (val < -512)
				val = -512;
			if (val < 0)
				val = 1024 + val;
			raw_write8(s->port, s->addr, BMI160_OFFSET_GYR70 + i,
					val & 0xFF);
			val98 &= ~(0x3 << (2 * i));
			val98 |= (val >> 8) << (2 * i);
		}
		ret = raw_write8(s->port, s->addr, BMI160_OFFSET_EN_GYR98,
				 val98 | BMI160_OFFSET_GYRO_EN);
		break;
#ifdef CONFIG_MAG_BMI160_BMM150
	case MOTIONSENSE_TYPE_MAG:
		ret = bmm150_set_offset(s, v);
		break;
#endif /* defined(CONFIG_MAG_BMI160) */
	default:
		ret = EC_RES_INVALID_PARAM;
	}
	return ret;
}

int perform_calib(const struct motion_sensor_t *s)
{
	int ret, val, en_flag, status, timeout = 0, rate;

	rate = get_data_rate(s);
	/*
	 * Temporary set frequency to 100Hz to get enough data in a short
	 * period of time.
	 */
	set_data_rate(s, 100000, 0);

	switch (s->type) {
	case MOTIONSENSE_TYPE_ACCEL:
		/* We assume the device is laying flat for calibration */
		val = (BMI160_FOC_ACC_0G << BMI160_FOC_ACC_X_OFFSET) |
			(BMI160_FOC_ACC_0G << BMI160_FOC_ACC_Y_OFFSET) |
			(BMI160_FOC_ACC_PLUS_1G << BMI160_FOC_ACC_Z_OFFSET);
		en_flag = BMI160_OFFSET_ACC_EN;
		break;
	case MOTIONSENSE_TYPE_GYRO:
		val = BMI160_FOC_GYRO_EN;
		en_flag = BMI160_OFFSET_GYRO_EN;
		break;
	default:
		/* Not supported on Magnetometer */
		ret = EC_RES_INVALID_PARAM;
		goto end_perform_calib;
	}
	ret = raw_write8(s->port, s->addr, BMI160_FOC_CONF, val);
	ret = raw_write8(s->port, s->addr, BMI160_CMD_REG,
			 BMI160_CMD_START_FOC);
	do {
		if (timeout > 400) {
			ret = EC_RES_TIMEOUT;
			goto end_perform_calib;
		}
		msleep(50);
		ret = raw_read8(s->port, s->addr, BMI160_STATUS, &status);
		if (ret != EC_SUCCESS)
			goto end_perform_calib;
		timeout += 50;
	} while ((status & BMI160_FOC_RDY) == 0);

	/* Calibration is successful, and loaded, use the result */
	ret = raw_read8(s->port, s->addr, BMI160_OFFSET_EN_GYR98, &val);
	ret = raw_write8(s->port, s->addr, BMI160_OFFSET_EN_GYR98,
			 val | en_flag);
end_perform_calib:
	set_data_rate(s, rate, 0);
	return ret;
}

void normalize(const struct motion_sensor_t *s, vector_3_t v, uint8_t *data)
{
#ifdef CONFIG_MAG_BMI160_BMM150
	if (s->type == MOTIONSENSE_TYPE_MAG)
		bmm150_normalize(s, v, data);
	else
#endif
	{
		v[0] = ((int16_t)((data[1] << 8) | data[0]));
		v[1] = ((int16_t)((data[3] << 8) | data[2]));
		v[2] = ((int16_t)((data[5] << 8) | data[4]));
	}
	rotate(v, *s->rot_standard_ref, v);
}

/*
 * Manage gesture recognition.
 * Defined even if host interface is not defined, to enable double tap even
 * when the host does not deal with gesture.
 */
int manage_activity(const struct motion_sensor_t *s,
		    enum motionsensor_activity activity,
		    int enable,
		    const struct ec_motion_sense_activity *param)
{
	int ret;
	struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);

	switch (activity) {
#ifdef CONFIG_GESTURE_SIGMO
	case MOTIONSENSE_ACTIVITY_SIG_MOTION: {
		int tmp;
		ret = raw_read8(s->port, s->addr, BMI160_INT_EN_0, &tmp);
		if (ret)
			return ret;
		if (enable) {
			/* We should use parameters from caller */
			raw_write8(s->port, s->addr, BMI160_INT_MOTION_3,
				BMI160_MOTION_PROOF_TIME(
					CONFIG_GESTURE_SIGMO_PROOF_MS) <<
				BMI160_MOTION_PROOF_OFF |
				BMI160_MOTION_SKIP_TIME(
					CONFIG_GESTURE_SIGMO_SKIP_MS) <<
				BMI160_MOTION_SKIP_OFF |
				BMI160_MOTION_SIG_MOT_SEL);
			raw_write8(s->port, s->addr, BMI160_INT_MOTION_1,
				BMI160_MOTION_TH(s,
					CONFIG_GESTURE_SIGMO_THRES_MG));
			tmp |= BMI160_INT_ANYMO_X_EN |
				BMI160_INT_ANYMO_Y_EN |
				BMI160_INT_ANYMO_Z_EN;
		} else {
			tmp &= ~(BMI160_INT_ANYMO_X_EN |
				 BMI160_INT_ANYMO_Y_EN |
				 BMI160_INT_ANYMO_Z_EN);
		}
		ret = raw_write8(s->port, s->addr, BMI160_INT_EN_0, tmp);
		if (ret)
			ret = EC_RES_UNAVAILABLE;
		break;
	}
#endif
#ifdef CONFIG_GESTURE_SENSOR_BATTERY_TAP
	case MOTIONSENSE_ACTIVITY_DOUBLE_TAP: {
		int tmp;
		/* Set double tap interrupt */
		ret = raw_read8(s->port, s->addr, BMI160_INT_EN_0, &tmp);
		if (ret)
			return ret;
		if (enable)
			tmp |= BMI160_INT_D_TAP_EN;
		else
			tmp &= ~BMI160_INT_D_TAP_EN;
		ret = raw_write8(s->port, s->addr, BMI160_INT_EN_0, tmp);
		if (ret)
			ret = EC_RES_UNAVAILABLE;
		break;
	}
#endif
	default:
		ret = EC_RES_INVALID_PARAM;
	}
	if (ret == EC_RES_SUCCESS) {
		if (enable) {
			data->enabled_activities |= 1 << activity;
			data->disabled_activities &= ~(1 << activity);
		} else {
			data->enabled_activities &= ~(1 << activity);
			data->disabled_activities |= 1 << activity;
		}
	}
	return ret;
}

#ifdef CONFIG_GESTURE_HOST_DETECTION
int list_activities(const struct motion_sensor_t *s,
		    uint32_t *enabled,
		    uint32_t *disabled)
{
	struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);
	*enabled = data->enabled_activities;
	*disabled = data->disabled_activities;
	return EC_RES_SUCCESS;
}
#endif

#ifdef CONFIG_ACCEL_INTERRUPTS
/**
 * bmi160_interrupt - called when the sensor activates the interrupt line.
 *
 * This is a "top half" interrupt handler, it just asks motion sense ask
 * to schedule the "bottom half", ->irq_handler().
 */
void bmi160_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ACCELGYRO_BMI160_INT_EVENT, 0);
}


static int config_interrupt(const struct motion_sensor_t *s)
{
	int ret, tmp;

	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_SUCCESS;

	mutex_lock(s->mutex);
	raw_write8(s->port, s->addr, BMI160_CMD_REG, BMI160_CMD_FIFO_FLUSH);
	msleep(30);
	raw_write8(s->port, s->addr, BMI160_CMD_REG, BMI160_CMD_INT_RESET);

#ifdef CONFIG_GESTURE_SENSOR_BATTERY_TAP
	raw_write8(s->port, s->addr, BMI160_INT_TAP_0,
		BMI160_TAP_TH(s, CONFIG_GESTURE_TAP_MAX_INTERSTICE_T));
	ret = raw_write8(s->port, s->addr, BMI160_INT_TAP_1,
		BMI160_TAP_TH(s, CONFIG_GESTURE_TAP_THRES_MG));
#endif
	/*
	 * configure int2 as an external input.
	 * Set a 5ms latch to be sure the EC can read the interrupt register
	 * properly, even when it is running more slowly.
	 */
	ret = raw_write8(s->port, s->addr, BMI160_INT_LATCH,
		BMI160_INT2_INPUT_EN | BMI160_LATCH_5MS);

	/* configure int1 as an interupt */
	ret = raw_write8(s->port, s->addr, BMI160_INT_OUT_CTRL,
		BMI160_INT_CTRL(1, OUTPUT_EN));

	/* Map activity interrupt to int 1 */
	tmp = 0;
#ifdef CONFIG_GESTURE_SIGMO
	tmp |= BMI160_INT_ANYMOTION;
#endif
#ifdef CONFIG_GESTURE_SENSOR_BATTERY_TAP
	tmp |= BMI160_INT_D_TAP;
#endif
	ret = raw_write8(s->port, s->addr, BMI160_INT_MAP_REG(1), tmp);

#ifdef CONFIG_ACCEL_FIFO
	/* map fifo water mark to int 1 */
	ret = raw_write8(s->port, s->addr, BMI160_INT_FIFO_MAP,
			BMI160_INT_MAP(1, FWM) |
			BMI160_INT_MAP(1, FFULL));

	/* configure fifo watermark at 50% */
	ret = raw_write8(s->port, s->addr, BMI160_FIFO_CONFIG_0,
			512 / sizeof(uint32_t));
	ret = raw_write8(s->port, s->addr, BMI160_FIFO_CONFIG_1,
			BMI160_FIFO_TAG_INT1_EN |
			BMI160_FIFO_TAG_INT2_EN |
			BMI160_FIFO_HEADER_EN);

	/* Set fifo*/
	ret = raw_read8(s->port, s->addr, BMI160_INT_EN_1, &tmp);
	tmp |= BMI160_INT_FWM_EN | BMI160_INT_FFUL_EN;
	ret = raw_write8(s->port, s->addr, BMI160_INT_EN_1, tmp);
#endif
	mutex_unlock(s->mutex);
	return ret;
}

/**
 * irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt.
 *
 * For now, we just print out. We should set a bitmask motion sense code will
 * act upon.
 */
static int irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int interrupt;

	if ((s->type != MOTIONSENSE_TYPE_ACCEL) ||
	    (!(*event & CONFIG_ACCELGYRO_BMI160_INT_EVENT)))
		return EC_ERROR_NOT_HANDLED;

	raw_read32(s->port, s->addr, BMI160_INT_STATUS_0, &interrupt);

#ifdef CONFIG_GESTURE_SENSOR_BATTERY_TAP
	if (interrupt & BMI160_D_TAP_INT)
		*event |= CONFIG_GESTURE_TAP_EVENT;
#endif
#ifdef CONFIG_GESTURE_SIGMO
	if (interrupt & BMI160_SIGMOT_INT)
		*event |= CONFIG_GESTURE_SIGMO_EVENT;
#endif
	/*
	 * No need to read the FIFO here, motion sense task is
	 * doing it on every interrupt.
	 */
	return EC_SUCCESS;
}

static int set_interrupt(const struct motion_sensor_t *s,
			       unsigned int threshold)
{
	/* Currently unsupported. */
	return EC_ERROR_UNKNOWN;
}
#endif  /* CONFIG_ACCEL_INTERRUPTS */

#ifdef CONFIG_ACCEL_FIFO
enum fifo_state {
	FIFO_HEADER,
	FIFO_DATA_SKIP,
	FIFO_DATA_TIME,
	FIFO_DATA_CONFIG,
};


#define BMI160_FIFO_BUFFER 64
static uint8_t bmi160_buffer[BMI160_FIFO_BUFFER];
#define BUFFER_END(_buffer) ((_buffer) + sizeof(_buffer))
/*
 * Decode the header from the fifo.
 * Return 0 if we need further processing.
 * Sensor mutex must be held during processing, to protect the fifos.
 *
 * @s: base sensor
 * @hdr: the header to decode
 * @bp: current pointer in the buffer, updated when processing the header.
 */
static int bmi160_decode_header(struct motion_sensor_t *s,
		enum fifo_header hdr, uint8_t **bp)
{
	if ((hdr & BMI160_FH_MODE_MASK) == BMI160_EMPTY &&
			(hdr & BMI160_FH_PARM_MASK) != 0) {
		int i, size = 0;
		/* Check if there is enough space for the data frame */
		for (i = MOTIONSENSE_TYPE_MAG; i >= MOTIONSENSE_TYPE_ACCEL;
		     i--) {
			if (hdr & (1 << (i + BMI160_FH_PARM_OFFSET)))
				size += (i == MOTIONSENSE_TYPE_MAG ? 8 : 6);
		}
		if (*bp + size > BUFFER_END(bmi160_buffer)) {
			/* frame is not complete, it
			 * will be retransmitted.
			 */
			*bp = BUFFER_END(bmi160_buffer);
			return 1;
		}
		for (i = MOTIONSENSE_TYPE_MAG; i >= MOTIONSENSE_TYPE_ACCEL;
		     i--) {
			if (hdr & (1 << (i + BMI160_FH_PARM_OFFSET))) {
				struct ec_response_motion_sensor_data vector;
				int *v = (s + i)->raw_xyz;
				vector.flags = 0;
				normalize(s + i, v, *bp);
				vector.data[X] = v[X];
				vector.data[Y] = v[Y];
				vector.data[Z] = v[Z];
				vector.sensor_num = i;
				motion_sense_fifo_add_unit(&vector, s + i, 3);
				*bp += (i == MOTIONSENSE_TYPE_MAG ? 8 : 6);
			}
		}
#if 0
		if (hdr & BMI160_FH_EXT_MASK)
			CPRINTF("%s%s\n",
				(hdr & 0x1 ? "INT1" : ""),
				(hdr & 0x2 ? "INT2" : ""));
#endif
		return 1;
	} else {
		return 0;
	}
}

static int load_fifo(struct motion_sensor_t *s)
{
	int done = 0;
	struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);

	if (s->type != MOTIONSENSE_TYPE_ACCEL)
		return EC_SUCCESS;

	do {
		enum fifo_state state = FIFO_HEADER;
		uint8_t *bp = bmi160_buffer;
		uint32_t beginning;

		if (!(data->flags &
		      (BMI160_FIFO_ALL_MASK << BMI160_FIFO_FLAG_OFFSET))) {
			/*
			 * The FIFO was disable while were processing it.
			 *
			 * Flush potential left over:
			 * When sensor is resumed, we won't read old data.
			 */
			raw_write8(s->port, s->addr, BMI160_CMD_REG,
				   BMI160_CMD_FIFO_FLUSH);
			return EC_SUCCESS;
		}

		raw_read_n(s->port, s->addr, BMI160_FIFO_DATA, bmi160_buffer,
				sizeof(bmi160_buffer));
		beginning = *(uint32_t *)bmi160_buffer;
		/*
		 * FIFO is invalid when reading while the sensors are all
		 * suspended.
		 * Instead of returning the empty frame, it can return a
		 * pattern that looks like a valid header: 84 or 40.
		 * If we see those, assume the sensors have been disabled
		 * while this thread was running.
		 */
		if (beginning == 0x84848484 ||
		    (beginning & 0xdcdcdcdc) == 0x40404040) {
			CPRINTS("Suspended FIFO: accel ODR/rate: %d/%d: 0x%08x",
				BASE_ODR(s->config[SENSOR_CONFIG_AP].odr),
				get_data_rate(s),
				beginning);
			return EC_SUCCESS;
		}

		while (!done && bp != BUFFER_END(bmi160_buffer)) {
			switch (state) {
			case FIFO_HEADER: {
				enum fifo_header hdr = *bp++;
				if (bmi160_decode_header(s, hdr, &bp))
					continue;
				/* Other cases */
				hdr &= 0xdc;
				switch (hdr) {
				case BMI160_EMPTY:
					done = 1;
					break;
				case BMI160_SKIP:
					state = FIFO_DATA_SKIP;
					break;
				case BMI160_TIME:
					state = FIFO_DATA_TIME;
					break;
				case BMI160_CONFIG:
					state = FIFO_DATA_CONFIG;
					break;
				default:
					CPRINTS("Unknown header: 0x%02x @ %d",
						hdr, bp - bmi160_buffer);
					raw_write8(s->port, s->addr,
						   BMI160_CMD_REG,
						   BMI160_CMD_FIFO_FLUSH);
					done = 1;
				}
				break;
			}
			case FIFO_DATA_SKIP:
				CPRINTS("skipped %d frames", *bp++);
				state = FIFO_HEADER;
				break;
			case FIFO_DATA_CONFIG:
				CPRINTS("config change: 0x%02x", *bp++);
				state = FIFO_HEADER;
				break;
			case FIFO_DATA_TIME:
				if (bp + 3 > BUFFER_END(bmi160_buffer)) {
					bp = BUFFER_END(bmi160_buffer);
					continue;
				}
				/* We are not requesting timestamp */
				CPRINTS("timestamp %d", (bp[2] << 16) |
					(bp[1] << 8) | bp[0]);
				state = FIFO_HEADER;
				bp += 3;
				break;
			default:
				CPRINTS("Unknown data: 0x%02x", *bp++);
				state = FIFO_HEADER;
			}
		}
	} while (!done);
	return EC_SUCCESS;
}
#endif  /* CONFIG_ACCEL_FIFO */


static int read(const struct motion_sensor_t *s, vector_3_t v)
{
	uint8_t data[6];
	int ret, status = 0;

	ret = raw_read8(s->port, s->addr, BMI160_STATUS, &status);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * If sensor data is not ready, return the previous read data.
	 * Note: return success so that motion senor task can read again
	 * to get the latest updated sensor data quickly.
	 */
	if (!(status & BMI160_DRDY_MASK(s->type))) {
		if (v != s->raw_xyz)
			memcpy(v, s->raw_xyz, sizeof(s->raw_xyz));
		return EC_SUCCESS;
	}

	/* Read 6 bytes starting at xyz_reg */
	ret = raw_read_n(s->port, s->addr, get_xyz_reg(s->type), data, 6);

	if (ret != EC_SUCCESS) {
		CPRINTF("[%T %s type:0x%X RD XYZ Error %d]",
			s->name, s->type, ret);
		return ret;
	}
	normalize(s, v, data);
	return EC_SUCCESS;
}

static int init(const struct motion_sensor_t *s)
{
	int ret = 0, tmp;

	ret = raw_read8(s->port, s->addr, BMI160_CHIP_ID, &tmp);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (tmp != BMI160_CHIP_ID_MAJOR && tmp != BMI168_CHIP_ID_MAJOR) {
		/* The device may be lock on paging mode. Try to unlock it. */
		raw_write8(s->port, s->addr, BMI160_CMD_REG,
				BMI160_CMD_EXT_MODE_EN_B0);
		raw_write8(s->port, s->addr, BMI160_CMD_REG,
				BMI160_CMD_EXT_MODE_EN_B1);
		raw_write8(s->port, s->addr, BMI160_CMD_REG,
				BMI160_CMD_EXT_MODE_EN_B2);
		raw_write8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
				BMI160_CMD_PAGING_EN);
		raw_write8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR, 0);
		return EC_ERROR_ACCESS_DENIED;
	}


	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
		struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);

		/* Reset the chip to be in a good state */
		raw_write8(s->port, s->addr, BMI160_CMD_REG,
				BMI160_CMD_SOFT_RESET);
		msleep(30);
		data->flags &= ~(BMI160_FLAG_SEC_I2C_ENABLED |
				(BMI160_FIFO_ALL_MASK <<
				 BMI160_FIFO_FLAG_OFFSET));
#ifdef CONFIG_GESTURE_HOST_DETECTION
		data->enabled_activities = 0;
		data->disabled_activities = 0;
#ifdef CONFIG_GESTURE_SIGMO
		data->disabled_activities |=
			1 << MOTIONSENSE_ACTIVITY_SIG_MOTION;
#endif
#ifdef CONFIG_GESTURE_SENSOR_BATTERY_TAP
		data->disabled_activities |=
			1 << MOTIONSENSE_ACTIVITY_DOUBLE_TAP;
#endif
#endif
		/* To avoid gyro wakeup */
		raw_write8(s->port, s->addr, BMI160_PMU_TRIGGER, 0);
	}

	raw_write8(s->port, s->addr, BMI160_CMD_REG,
			BMI160_CMD_MODE_NORMAL(s->type));
	msleep(wakeup_time[s->type]);

#ifdef CONFIG_MAG_BMI160_BMM150
	if (s->type == MOTIONSENSE_TYPE_MAG) {
		struct bmi160_drv_data_t *data = BMI160_GET_DATA(s);
		if ((data->flags & BMI160_FLAG_SEC_I2C_ENABLED) == 0) {
			int ext_page_reg, pullup_reg;
			/* Enable secondary interface */
			/*
			 * This is not part of the normal configuration but from
			 * code on Bosh github repo:
			 * https://github.com/BoschSensortec/BMI160_driver
			 *
			 * Magic command sequences
			 */
			raw_write8(s->port, s->addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B0);
			raw_write8(s->port, s->addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B1);
			raw_write8(s->port, s->addr, BMI160_CMD_REG,
					BMI160_CMD_EXT_MODE_EN_B2);

			/*
			 * Change the register page to target mode, to change
			 * the internal pull ups of the secondary interface.
			 */
			raw_read8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg | BMI160_CMD_TARGET_PAGE);
			raw_read8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg | BMI160_CMD_PAGING_EN);
			raw_read8(s->port, s->addr, BMI160_COM_C_TRIM_ADDR,
					&pullup_reg);
			raw_write8(s->port, s->addr, BMI160_COM_C_TRIM_ADDR,
					pullup_reg | BMI160_COM_C_TRIM);
			raw_read8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);
			raw_write8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
					ext_page_reg & ~BMI160_CMD_TARGET_PAGE);
			raw_read8(s->port, s->addr, BMI160_CMD_EXT_MODE_ADDR,
					&ext_page_reg);

			/* Set the i2c address of the compass */
			ret = raw_write8(s->port, s->addr, BMI160_MAG_IF_0,
					BMM150_I2C_ADDRESS);

			/* Enable the secondary interface as I2C */
			ret = raw_write8(s->port, s->addr, BMI160_IF_CONF,
				BMI160_IF_MODE_AUTO_I2C << BMI160_IF_MODE_OFF);
			data->flags |= BMI160_FLAG_SEC_I2C_ENABLED;
		}


		bmm150_mag_access_ctrl(s->port, s->addr, 1);

		ret = bmm150_init(s);
		if (ret)
			/* Leave the compass open for tinkering. */
			return ret;

		/* Leave the address for reading the data */
		raw_write8(s->port, s->addr, BMI160_MAG_I2C_READ_ADDR,
				BMM150_BASE_DATA);
		/*
		 * Put back the secondary interface in normal mode.
		 * BMI160 will poll based on the configure ODR.
		 */
		bmm150_mag_access_ctrl(s->port, s->addr, 0);
	}
#endif

	set_range(s, s->default_range, 0);

	if (s->type == MOTIONSENSE_TYPE_ACCEL) {
#ifdef CONFIG_ACCEL_INTERRUPTS
		ret = config_interrupt(s);
#endif
	}
	CPRINTF("[%T %s: MS Done Init type:0x%X range:%d]\n",
			s->name, s->type, get_range(s));
	return ret;
}

const struct accelgyro_drv bmi160_drv = {
	.init = init,
	.read = read,
	.set_range = set_range,
	.get_range = get_range,
	.set_resolution = set_resolution,
	.get_resolution = get_resolution,
	.set_data_rate = set_data_rate,
	.get_data_rate = get_data_rate,
	.set_offset = set_offset,
	.get_offset = get_offset,
	.perform_calib = perform_calib,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.set_interrupt = set_interrupt,
	.irq_handler = irq_handler,
#endif
#ifdef CONFIG_ACCEL_FIFO
	.load_fifo = load_fifo,
#endif
#ifdef CONFIG_GESTURE_HOST_DETECTION
	.manage_activity = manage_activity,
	.list_activities = list_activities,
#endif
};

struct bmi160_drv_data_t g_bmi160_data = {
	.flags = 0,
};
