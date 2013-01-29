/*
 * rgbdlocalization.h
 *
 *  Created on: Jan 28, 2013
 *      Author: lucasrangit
 */

#ifndef RGBDLOCALIZATION_H_
#define RGBDLOCALIZATION_H_

/*
 * How many frames to process per second depends on the application.
 * Tune this based of the performance of the CPU.
 * Sensor over USB cannot handle more than 30 FPS.
 */
enum {
	PROCESS_FPS = 2,
	CONTOUR_AREA_MIN = 1000, // @TODO automatically determine area for smallest and largest ceiling light
	CONTOUR_AREA_MAX = 10000,
	CONTOUR_AREA_DIFFERENCE 	= 500,	// maximum difference between the potential matching contours
	CONTOUR_POSITION_DIFFERENCE = 100	// maximum
};

// Kinect's maximum tilt (from libfreenect header)
enum { MAX_TILT_ANGLE = 31 };

// Channel index for BGR images
enum {
	BGR_BLUE_INDEX	= 0,
	BGR_GREEN_INDEX	= 1,
	BGR_RED_INDEX	= 3
};

struct stats {
	int count;
	double average;
	double min;
	double max;
};
typedef enum stats_array_index {
	RGB_CONTOURS = 0,
	DEPTH_CONTOURS,
	STATS_ARRAY_DIMENSIONS
} stats_array_index;

enum { LANDMARK_COUNT_MAX = 10 };

#endif /* RGBDLOCALIZATION_H_ */
