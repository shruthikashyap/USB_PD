/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for motion_lid.h */

#ifndef __CROS_EC_MOTION_LID_H
#define __CROS_EC_MOTION_LID_H

#include "host_command.h"

/**
 * This structure defines all of the data needed to specify the orientation
 * of the base and lid accelerometers in order to calculate the lid angle.
 */
struct accel_orientation {
	/* Rotation matrix to rotate positive 90 degrees around the hinge. */
	matrix_3x3_t rot_hinge_90;

	/*
	 * Rotation matrix to rotate 180 degrees around the hinge. The value
	 * here should be rot_hinge_90 ^ 2.
	 */
	matrix_3x3_t rot_hinge_180;

	/* Vector pointing along hinge axis. */
	vector_3_t hinge_axis;
};

/* Link global structure for orientation. This must be defined in board.c. */
extern const struct accel_orientation acc_orient;


/**
 * Get last calculated lid angle. Note, the lid angle calculated by the EC
 * is un-calibrated and is an approximate angle.
 *
 * @return lid angle in degrees in range [0, 360], or LID_ANGLE_UNRELIABLE
 * if the lid angle can't be determined.
 */
int motion_lid_get_angle(void);

int host_cmd_motion_lid(struct host_cmd_handler_args *args);

void motion_lid_calc(void);

#endif  /* __CROS_EC_MOTION_LID_H */


