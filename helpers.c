/*
 * helpers.c
 *
 *  Created on: Jan 28, 2013
 *      Author: lucasrangit
 */
#include "rgbdlocalization.h"

#if USE_INVERSE_DEPTH_APPROXIMATION
/*
 * From their data, a basic first order approximation for converting the raw
 * 11-bit disparity value to a depth value in centimeters is:
 *   100/(-0.00307 * rawDisparity + 3.33).
 * This approximation is approximately 10 cm off at 4 m away, and less than
 * 2 cm off within 2.5 m.
 */
float raw_depth_to_meters(int raw_disparity)
{
	float depth = 1.0 / (raw_disparity * -0.0030711016 + 3.3309495161);
	return depth;
}
#else
/*
 * A better approximation given by Stéphane Magnenat:
 *   distance = 0.1236 * tan(rawDisparity / 2842.5 + 1.1863) in meters.
 * Adding a final offset term of -0.037 centers the original ROS data.
 * The tan approximation has a sum squared difference of .33 cm while the
 * 1/x approximation is about 1.7 cm.
 *
 */
float raw_depth_to_meters(int raw_disparity)
{
	float depth = 0.1236 * tanf(raw_disparity/2842.5 + 1.1863);
	return depth;
}
#endif

/*
 * Color only value for disparity values in the ranges of interest.
 * Specifically, keep only the "no data" range.
 *
 * The Kinect's disparity data is 11-bits and stored in a 16-bit/1-channel image.
 * The filter's output should be a 8-bit/1-channel (monochrome) image.
 *
 * @todo remove right no data band
 */
void filter_out_of_range_disparity(IplImage *disparity_16u_1, IplImage *image_8u_1)
{
	unsigned char *depth_data = (unsigned char*)(image_8u_1->imageData);
	int i;
	for (i = 0; i < 640*480; i++)
	{
		int raw_disparity = ((short *)disparity_16u_1->imageData)[i];
		/**
		 * unknown: 	(raw_disparity < 242) 	// (depth_meters < 0.4)
		 * too close: 	(raw_disparity < 658) 	// (depth_meters < 0.8)
		 * normal: 		(raw_disparity < 1006) 	// (depth_meters < 4.0)
		 * too far: 	(raw_disparity < 1050) 	// (depth_meters < 8.0)
		 * unknown: 	(raw_disparity >= 1050)	// (depth_meters >= 8.0)
		 */
		if (raw_disparity >= 1050) // (depth_meters >= 8.0)
			depth_data[i] = 255; // white
		else
			depth_data[i] = 0;  // black
		// @TODO experiment with setting anything equal to 2047 to 1 only.
	}
}

void get_cv_info()
{
	const char* libraries;
	const char* modules;
	// Using cvGetModuleInfo() to check for IPP
	cvGetModuleInfo(0, &libraries, &modules);
	printf("Libraries: %s\nModules: %s\n", libraries, modules);
}

void tilt_wait_degs(int degrees)
{
	freenect_raw_tilt_state *state = 0;

	if (freenect_sync_set_tilt_degs(degrees, KINECT_INDEX_0)) {
		printf("Error: Kinect not connected?\n");
		return;
	}

	// wait for motor to stop moving
	do {
		if (freenect_sync_get_tilt_state(&state, KINECT_INDEX_0)) {
			printf("Error: Kinect not connected?\n");
			return;
		}
	} while (TILT_STATUS_MOVING == state->tilt_status);

//	sleep(1); // @bug motor doesn't report correct state
}

/**
 * Tilt the Kinect to the maximum angle.
 */
void tilt_up()
{
	tilt_wait_degs(MAX_TILT_ANGLE-10);
}

/**
 * Return the Kinect to the horizontal position.
 * If the Kinect is tilted, such as by a tripod, this will return the camera to
 * the horizontal position relative to the ground, not the base of the Kinect.
 */
void tilt_horizontal()
{
	tilt_wait_degs(0);
}

/*
 * Handle Key Input
 * Note, after making certain changes (such as shifting) any images that
 * are comprised of multiple samples must be cleared.
 * Returns if a re-initialization is necessary.
 */
bool handle_key_input(char key, int *x_offset, int *y_offset)
{
	bool reinitialize = false;

	switch (key)
	{
	case 81: // left
		*x_offset -= 1;
		reinitialize = true;
		break;
	case 82: // up
		*y_offset -= 1;
		reinitialize = true;
		break;
	case 83: // right
		*x_offset += 1;
		reinitialize = true;
		break;
	case 84: // down
		*y_offset += 1;
		reinitialize = true;
		break;
	case '_':
	case '-':
		// scale down
		// @todo
		reinitialize = true;
		break;
	case '+':
	case '=':
		// scale up
		// @todo
		reinitialize = true;
		break;
	case 'c':
		// clear
		reinitialize = true;
		break;
	case 'q':
		// main loop will handle exit
	case -1:
		// no key
	default:
		// nothing to adjust
		break;
	}

	return reinitialize;
}

void mouseHandler(int event, int x, int y, int flags, void *param)
{
	CvPoint* mouse_click = param;
	bool handled = true;

	switch (event)
	{
	case CV_EVENT_LBUTTONDOWN:
		/* left button down */
//		fprintf(stdout, "Left button down");
		mouse_click->x = x;
		mouse_click->y = y;
		break;
	case CV_EVENT_LBUTTONDBLCLK:
		break;
	case CV_EVENT_RBUTTONDOWN:
		/* right button down */
//		fprintf(stdout, "Right button down");
		break;
	case CV_EVENT_RBUTTONDBLCLK:
//		fprintf(stdout, "Right button double-click");
		break;
	case CV_EVENT_MOUSEMOVE:
		/* mouse move */
	default:
		/* unhandled event */
		handled = false;
		break;
	}

	if (handled)
		fprintf(stdout, "Unhandled mouse event at (%d, %d).\n", x, y);
}

void shift_image( IplImage *image_src, int x_offset, int y_offset)
{
	IplImage *image_shifted = cvCreateImage( cvGetSize(image_src), image_src->depth, image_src->nChannels);

	if (0 == x_offset && 0 == y_offset)
	{
		goto release;
	}
	else if (x_offset >= 0 && y_offset >= 0)
	{
		cvSetImageROI(image_shifted, cvRect(abs(x_offset), abs(y_offset), 640, 480) );
		cvSetImageROI(image_src, cvRect(0, 0, 640-abs(x_offset), 480-abs(y_offset)) );
	}
	else if (x_offset >= 0 && y_offset < 0)
	{
		cvSetImageROI(image_shifted, cvRect( abs(x_offset), 0, 640-abs(x_offset), 480-abs(y_offset) ) );
		cvSetImageROI(image_src, cvRect( 0, abs(y_offset), 640-abs(x_offset), 480-abs(y_offset) ) );
	}
	else if (x_offset < 0 && y_offset >= 0)
	{
		cvSetImageROI(image_shifted, cvRect( 0, abs(y_offset), 640-abs(x_offset), 480-abs(y_offset) ) );
		cvSetImageROI(image_src, cvRect( abs(x_offset), 0, 640-abs(x_offset), 480-abs(y_offset) ) );
	}
	else if (x_offset < 0 && y_offset < 0)
	{
		cvSetImageROI(image_shifted, cvRect(0, 0, 640-abs(x_offset), 480-abs(y_offset)));
		cvSetImageROI(image_src, cvRect(abs(x_offset),abs(y_offset),640-abs(x_offset),480-abs(y_offset)));
	}

	cvCopy( image_src, image_shifted, NULL);

	cvResetImageROI(image_shifted);
	cvResetImageROI(image_src);

	cvCopy( image_shifted, image_src, NULL);

release:
	cvReleaseImage( &image_shifted);
}

int acquire_color_and_disparity( IplImage *image_dst_color, IplImage *image_dst_disparity)
{
	int error_code = 0;
	IplImage *image_rgb = NULL;
	IplImage *image_disparity = NULL;

	image_rgb = freenect_sync_get_rgb_cv(KINECT_INDEX_0);
	if (!image_rgb) {
		printf("Error: Kinect not connected?\n");
		error_code = -1;
		goto abort;
	}

	image_disparity = freenect_sync_get_depth_cv(KINECT_INDEX_0);
	if (!image_disparity) {
		printf("Error: Kinect not connected?\n");
		error_code = -1;
		goto abort;
	}

	cvCvtColor(image_rgb, image_rgb, CV_RGB2BGR);
	cvCopy( image_rgb, image_dst_color, NULL);

	cvCopy( image_disparity, image_dst_disparity, NULL);

	abort:
	return error_code;
}

void draw_value( IplImage *image, float value, CvPoint pixel)
{
	if (0 > pixel.x || 0 > pixel.y)
		// invalid pixel coordinate
		return;

	CvFont font;
	cvInitFont(&font, CV_FONT_HERSHEY_COMPLEX_SMALL, 0.5, 0.5, 0, 1, CV_AA);
	char coord_str[] = "123,123,1234.1234"; // max string length

	if (-1 != value)
	{
		// print coordinate with a value
		sprintf(coord_str, "%03d,%03d,%04.4f", pixel.x, pixel.y, value);
	}
	else
	{
		// print just the coordinate without a value
		sprintf(coord_str, "%03d,%03d", pixel.x, pixel.y);
	}

	cvPutText(image, coord_str, pixel, &font, cvScalar(255, 255, 255, 0));
}

float distance2f( CvPoint a, CvPoint b)
{
	float distance = 0.0;

	distance = sqrtf( pow(b.x - a.x, 2) + pow(b.y - a.y, 2) );

	if ( 0 > distance || distance > 640)
	{
		// something wrong
		distance = -1.0;
	}

	return distance;
}


float scale_cartician( float point, float scale, float center)
{
	return (point*scale + center*(1-scale));
}

int get_disparity( IplImage *disparity, CvPoint coord)
{
	int pixel_disparity = -1;

	if ( coord.x < 0 || 640 < coord.x)
	{
		// out of range
		goto exit;
	}
	else if ( coord.y < 0 || 480 < coord.y)
	{
		// out of range
		goto exit;
	}
	else
	{
		pixel_disparity = ((short *) disparity->imageData)[coord.y * 640 + coord.x];
	}

	exit:
	return pixel_disparity;
}

/**
 * Extract a column vector from a matrix.
 */
int get_vector_column( CvMat *input_matrix, CvMat *output_vector, int column)
{
	int error_code = 0;
	int i = 0;

	if ( input_matrix->rows != output_vector->rows)
		assert("rows must match");

	for ( i = 0; i < input_matrix->rows; ++i)
	{
		*( (float*)CV_MAT_ELEM_PTR( *output_vector, i, 0 ) ) =
				CV_MAT_ELEM( *input_matrix, float, i, column );
	}

	return error_code;
}
