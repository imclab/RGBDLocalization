#include "rgbdlocalization.h"
#include "helpers.h"

/**
 * Quadrilateral Centroid Algorithm
 *
 * @author Filipi Vianna
 * @ref http://filipivianna.blogspot.com/2009/11/quadrilateral-centroid-algorithm.html
 */
CvPoint findCentroid( quad_coord input_quad)
{
	float verticesX[5];
	float verticesY[5];
	CvPoint2D32f centroid = { .x = 0, .y = 0 };

	if ( QC_INVALID == input_quad.valid)
		goto exit;

	verticesX[0] = input_quad.vertices[0].x;
	verticesY[0] = input_quad.vertices[0].y;
	verticesX[1] = input_quad.vertices[1].x;
	verticesY[1] = input_quad.vertices[1].y;
	verticesX[2] = input_quad.vertices[2].x;
	verticesY[2] = input_quad.vertices[2].y;
	verticesX[3] = input_quad.vertices[3].x;
	verticesY[3] = input_quad.vertices[3].y;
	// Repeat the first vertex
	verticesX[4] = verticesX[0];
	verticesY[4] = verticesY[0];

	int i, k;
	float area = 0.0f;
	float tmp = 0.0f;

	for (i = 0; i <= 4; i++){
		k = (i + 1) % (4 + 1);
		tmp = verticesX[i] * verticesY[k] -
				verticesX[k] * verticesY[i];
		area += tmp;
		centroid.x += (verticesX[i] + verticesX[k]) * tmp;
		centroid.y += (verticesY[i] + verticesY[k]) * tmp;
	}
	area *= 0.5f; // TODO why < 0 sometimes? vertices out of order?
	centroid.x *= 1.0f / (6.0f * area);
	centroid.y *= 1.0f / (6.0f * area);

//	printf("Centroid = (%1.2f, %1.2f),  area = %1.2f\n", centroid.x, centroid.y, area);

	exit:
	return cvPoint( (int)centroid.x, (int)centroid.y);
}

/**
 * Dilate a quadrilateral about a point.
 * TODO: evaluate another algorithm that works for convex or concave:
 *       http://stackoverflow.com/questions/7995547/enlarge-and-restrict-a-quadrilateral-polygon-in-opencv-2-3-with-c
 * @param[in] verticies going ccw
 */
quad_coord dilateQuadAboutCenter( quad_coord quad, float scale)
{
	CvPoint origin = findCentroid( quad);
	quad_coord scaled_quad;

	scaled_quad.vertices[0].x = scale_cartician(quad.vertices[0].x, scale, origin.x);
	scaled_quad.vertices[0].y = scale_cartician(quad.vertices[0].y, scale, origin.y);
	scaled_quad.vertices[1].x = scale_cartician(quad.vertices[1].x, scale, origin.x);
	scaled_quad.vertices[1].y = scale_cartician(quad.vertices[1].y, scale, origin.y);
	scaled_quad.vertices[2].x = scale_cartician(quad.vertices[2].x, scale, origin.x);
	scaled_quad.vertices[2].y = scale_cartician(quad.vertices[2].y, scale, origin.y);
	scaled_quad.vertices[3].x = scale_cartician(quad.vertices[3].x, scale, origin.x);
	scaled_quad.vertices[3].y = scale_cartician(quad.vertices[3].y, scale, origin.y);

	return scaled_quad;
}

void quad_coord_clear(quad_coord lights_depth[4])
{
	int quad;
	for (quad = 0; quad < LANDMARK_COUNT_MAX; ++quad)
	{
		int vertex;

		// by default all quads are invalid
		lights_depth[quad].valid = QC_INVALID;

		// for safety, iniatialize to a valid coordinate
		for (vertex = 0; vertex < 4; ++vertex)
		{
			lights_depth[quad].vertices[vertex].x = 0;
			lights_depth[quad].vertices[vertex].y = 0;
		}
	}
}

float approximate_depth( IplImage *disparity, quad_coord quad)
{
	float depth = -1.0;
	float vertex_depths[4] = { 0 };
	int i;

	/*
	 * Set the depth for each vertex.
	 * If a vertex does not have a valid depth dilate the quad until one is found.
	 */
	for ( i = 0; i < 4; ++i)
	{
		if ( QC_INVALID == quad.valid)
		{
			// quad has been flagged as invalid
			goto exit;
		}

		int pixel_disparity = get_disparity( disparity, quad.vertices[i]);
		if ( pixel_disparity > 0 && pixel_disparity < 2047)
			vertex_depths[i] = pixel_disparity;
		else
		{
			// grow each vertex until a known depth is found
			int looplimit = 100;
			do
			{
				quad_coord scaled_quad = dilateQuadAboutCenter( quad, 1.5);

				pixel_disparity = get_disparity( disparity, scaled_quad.vertices[i]);

				// stop if the dilation is out of range
				if ( -1 == pixel_disparity)
					// invalid parameter, out of range?
					goto exit;
				else if ( pixel_disparity >= 0 && pixel_disparity <= 2047) // change to useful range
					vertex_depths[i] = pixel_disparity; // TODO use disparity2depth

				if (!looplimit--)
				{
					// could not find a valid depth (does the sensor have full view of the quad?)
					vertex_depths[i] = -1.0;
					continue;
				}
			} while ( 0 == vertex_depths[i] );
		}
	}

	// return average of known depth
	for ( i = 0; i < 4; ++i)
	{
		depth += vertex_depths[i];
	}
	depth /= i;

	depth = raw_depth_to_meters(depth);

	exit:
	return depth;
}

/*
 * Find contours in an image and return a new image with the contours drawn.
 * This function performs a filter on the shapes to identify quadrilaterals
 * in the shape of a ceiling light: is convex, has 4 vertices, and a certain size.
 * TODO: consider adding a slider to the window for adjusting the thresholds
 */
static IplImage* detect_contours(IplImage* img, quad_coord *found_quads)
{
	CvSeq* contours; // linked list of contours
	CvSeq* polygon; // pointer to single polygon contour
	CvMemStorage *storage = cvCreateMemStorage(0); // storage for contour linked list
	static IplImage* ret = NULL;
	if (!ret)
		ret = cvCreateImage(cvGetSize(img), IPL_DEPTH_8U, 1);
	else
		cvZero(ret);
	IplImage* temp = cvCreateImage(cvGetSize(img), IPL_DEPTH_8U, 1);
	int i;
	double area;
	int contour_index = 0;

	/*
	 * Search input image for contours
	 */
	cvCopy(img, temp, NULL);
	// Find only the outer contours
	cvFindContours(temp, storage, &contours, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, cvPoint(0,0));

	/*
	 * Find contours that have the shape of ceiling lights
	 */
	while (contours)
	{
		// Approximate a polygon around contour using the Douglas-Peucker (DP) approximation.
		// Pass a zero as the last argument instructs cvApproxPoly to only operate on the first
		// element of the sequence.
		polygon = cvApproxPoly(contours, sizeof(CvContour), storage, CV_POLY_APPROX_DP, cvContourPerimeter(contours)*CONTOUR_PERIMETER_PERCENT/100, 0);

		if (4 == polygon->total)
		{
			// has 4 vertices
			area = fabs(cvContourArea(polygon, CV_WHOLE_SEQ, 0));
			if ((CONTOUR_AREA_MIN < area) && (area < CONTOUR_AREA_MAX))
			{
				// has "reasonable" area
				if (cvCheckContourConvexity(polygon))
				{
					// is convex
					CvPoint *pt[4];
					for ( i = 0; i < 4; i++)
					{
						// total number of vertices is 4, verified earlier
						pt[i] = (CvPoint*)cvGetSeqElem(polygon, i);

						// save the contour as a potential landmark
						// only save up to LANMARK_COUNT_MAX
						if (contour_index < LANDMARK_COUNT_MAX)
						{
							found_quads[contour_index].vertices[i] = *pt[i];
						}
					}
					found_quads[contour_index].valid = QC_VALID;

					// draw contour using the vertices so that we can adjust the color and thickness of each
					{
						CvScalar line_color = cvScalarAll(255); // use with black and white image
						int line_thickness = contour_index+1; // vary the thickness so that contours can be distinguished in black and white
						cvLine(ret, *pt[0], *pt[1], line_color, line_thickness, 8, 0);
						cvLine(ret, *pt[1], *pt[2], line_color, line_thickness, 8, 0);
						cvLine(ret, *pt[2], *pt[3], line_color, line_thickness, 8, 0);
						cvLine(ret, *pt[3], *pt[0], line_color, line_thickness, 8, 0);
					}

//					fprintf(stdout, "%d. (%03d,%03d) (%03d,%03d) (%03d,%03d) (%03d,%03d) area: %.1f\n", contour_index, pt[0]->x, pt[0]->y, pt[1]->x, pt[1]->y, pt[2]->x, pt[2]->y, pt[3]->x, pt[3]->y, area);
					contour_index++;
				}
			}
		}

		contours = contours->h_next;
	}

	cvReleaseImage(&temp);
	cvReleaseMemStorage(&storage);

	return ret;
}

/**
 * Estimate the 3D position based on the locations of 3 or more reference satellites.
 * The delta time component is 0 because the distances are measured simultaneously.
 *
 * This algorithm was from X and was written for Matlab by Dr. Jidong Huang
 * and converted to C for OpenCV by Lucas Magasweran.
 *
 * @ref APPENDIX A LINEARIZING THE GPS PSEUDORANGE EQUATIONS
 */
//function [xyz] = solve3D(svrange, svpos)
void solve3D( CvMat *svrange, CvMat *svpos)
{
	int i = 0, j = 0;
//eps = 1e-10;
	float eps = powf(10,-10);
//% setup the maximum number of iterations
//max_iter = 10;
	int max_iter = 10;
//
//% number of measurements
//num_sv = length(svrange);
	int num_sv = svrange->rows;

//iter = 0; % number of iterations
	int iter = 0;

//err_est = 100; % a large error to start
	float err_est = 100;

//% initial estimate from location [0 0 0]
//xyz = [0 0 0]';
	CvMat* xyz = cvCreateMat( 3, 1, CV_32FC1 );
	cvSetZero(xyz);

//% setup the Geometry matrix H, H is a num_sv x 3 matrix
	CvMat* H = cvCreateMat( num_sv, 3, CV_32FC1 );
	cvSetZero(H);

	CvMat* dRi = cvCreateMat( num_sv, 1, CV_32FC1 );
	cvSetZero(dRi);

	CvMat* svpos_i = cvCreateMat( 3, 1, CV_32FC1 );
	double svpos_i_norm = 0;
	CvMat* delta_xyz = cvCreateMat( 3, 1, CV_32FC1 );

//while ((iter<max_iter) & (err_est>eps))
	while ( (iter < max_iter) && (err_est > eps))
	{
//    iter = iter + 1;
		iter++;

//    for i = 1:num_sv
		for (i = 0; i < num_sv; ++i)
		{
//        Ri = norm(xyz - svpos(:, i));
			cvSetZero(svpos_i);
			get_vector_column( svpos, svpos_i, i);
			double Ri = cvNorm(xyz, svpos_i, CV_L2, NULL);

//        H(i, 1) = (xyz(1) - svpos(1, i))/Ri;
//        H(i, 2) = (xyz(2) - svpos(2, i))/Ri;
//        H(i, 3) = (xyz(3) - svpos(3, i))/Ri;
			for (j = 0; j < 3; ++j)
			{
				*( (float*)CV_MAT_ELEM_PTR( *H, i, j ) ) =
					(CV_MAT_ELEM( *xyz, float, j, 0 ) - CV_MAT_ELEM( *svpos, float, j, i )) / Ri;
			}

//        % setup the dRi vector, which is num_sv x 1 vector
//        dRi(i, 1) = svrange(i) - Ri;
			*( (float*)CV_MAT_ELEM_PTR( *dRi, i, 0 ) ) =
					CV_MAT_ELEM( *svrange, float, i, 0 ) - Ri;
//    end
		}
//
//    % calculate the corrections to xyz vector
		cvSetZero(delta_xyz);
//    delta_xyz = inv(H'*H)*H'*dRi;
		//int return_code =
				cvSolve( H, dRi, delta_xyz, CV_SVD);
//		if (return_code)
//		{
//			printf("solve3D: cvSolve failed\n");
//			return;
//		}

		for (j = 0; j < 3; ++j)
		{
//    % apply the corrections to xyz vector
//    xyz = xyz + delta_xyz;
			*( (float*)CV_MAT_ELEM_PTR( *xyz, j, 0 ) ) =
					CV_MAT_ELEM( *xyz, float, j, 0 ) + CV_MAT_ELEM( *delta_xyz, float, j, 0 );
		}

//    	err_est = norm(xyz - svpos(:, i)) - svrange(i);
		cvSetZero(svpos_i);
		get_vector_column( svpos, svpos_i, 2); // use the 3rd position to estimate error
		svpos_i_norm = cvNorm(xyz, svpos_i, CV_L2, NULL);
		err_est = svpos_i_norm - CV_MAT_ELEM( *svrange, float, 2, 0 );
//end
	}

	// display results
	float x = CV_MAT_ELEM( *xyz, float, 0, 0 );
	float y = CV_MAT_ELEM( *xyz, float, 1, 0 );
	float z = CV_MAT_ELEM( *xyz, float, 2, 0 );

	printf("%f, %f, %f\n", x, y, z);

	// clean-up
	cvReleaseMat(&xyz);
	cvReleaseMat(&H);
	cvReleaseMat(&dRi);
	cvReleaseMat(&svpos_i);
	cvReleaseMat(&delta_xyz);
}

int main(int argc, char *argv[])
{
	IplImage *image_rgb = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 3);
	IplImage *image_disparity = cvCreateImage( cvSize(640, 480), IPL_DEPTH_16U, 1);
	IplImage *image_nodepth = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 1);
	IplImage *image_nodepth_mask = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 1);
	IplImage *image_nodepth_color = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 3);
	IplImage *image_blended = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 3);
	IplImage *disparity_contours = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 1);
	IplImage *image_gray = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 1);
	IplImage *image_edges = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 1);
	IplImage *rgb_contours = cvCreateImage( cvSize(640, 480), IPL_DEPTH_8U, 1);
	bool reinitialize = true;
	char key;
	int x_offset = 0;
	int y_offset = 0;
	const char window_name_live[] = "Kinect Color and Depth";
	cvNamedWindow( window_name_live, CV_WINDOW_AUTOSIZE);
	CvPoint mouse_click = { .x = -1, .y = -1 };
	cvSetMouseCallback( window_name_live, mouseHandler, &mouse_click );
	int i;
	bool sparse = false;

	// unknown user position
	CvMat* user = cvCreateMat( 3, 1, CV_32FC1 );
	cvSetZero(user);
	// TODO change to 3D point
//	CvPoint3D32f user = { .x = 0, .y = 0, .z = 0 };

	// known landmark/light positions
	// [xi, yi, zi]'
	CvMat* svpos = cvCreateMat( 3, 4, CV_32FC1 );
	cvSetZero(svpos);
	// simulated lights experimental configuration
	// bottom left
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 0, 0 ) ) = 5.193;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 1, 0 ) ) = 2.438;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 2, 0 ) ) = 2.905;
	// bottom right
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 0, 1 ) ) = 5.193;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 1, 1 ) ) = 0.0;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 2, 1 ) ) = 2.905;
	// top left
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 0, 2 ) ) = 3.364;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 1, 2 ) ) = 2.438;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 2, 2 ) ) = 2.905;
	// top right
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 0, 3 ) ) = 3.364;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 1, 3 ) ) = 0.0;
	*( (float*)CV_MAT_ELEM_PTR( *svpos, 2, 3 ) ) = 2.905;

	// TODO change to array of 3D points
//	CvPoint3D32f lights[3];
//	lights[0].x = 0; lights[0].y = 0; lights[0].z = 0;
//	lights[1].x = 0; lights[1].y = 0; lights[1].z = 0;
//	lights[2].x = 0; lights[2].y = 0; lights[2].z = 0;

	// measured landmark/light distances
	float depths[4] = {0};
	CvMat* svrange = cvCreateMat( LANDMARK_COUNT_MAX, 1, CV_32FC1 );

	// Point the Kinect at the ceiling for a better view of the lights closest to it
	tilt_up();

	// process frames indefinitely at the rate defined by PROCESS_FPS
	// quit when user presses 'q'
	while (key != 'q')
	{
		/*
		 * Capture data from Kinect
		 */
		if (acquire_color_and_disparity( image_rgb, image_disparity ))
		{
			// error getting data, skip processing this frame
			goto waitloop;
		}
//		cvShowImage( "RGB", image_rgb);

		/*
		 * Filter raw depth image to create "No Depth" images
		 */
		filter_out_of_range_disparity(image_disparity, image_nodepth);

		// offset new depth image based off user input
		shift_image( image_nodepth, x_offset, y_offset);

		// take multiple samples of the missing depth data because it's noisy
		cvAdd( image_nodepth, image_nodepth_mask, image_nodepth_mask, NULL); // adding _grows_ the mask
		//cvAnd( image_depth, image_depth_smooth, image_depth_smooth, NULL); // anding _shrinks_ the mask
		// TODO: use weighted samples (e.g. 3 successive pixels must match)

		/*
		 * Find polygons in the disparity data
		 */
		quad_coord lights_depth[4];
		quad_coord_clear(lights_depth);
//		fprintf(stdout, "Disparity Contours (X,Y)\n");
		cvCopy( detect_contours(image_nodepth_mask, lights_depth), disparity_contours, NULL);

		/*
		 * find polygons in the RGB data
		 */
		// apply Gaussian blur to reduce noise
		cvSmooth(image_rgb, image_rgb, CV_GAUSSIAN, 5, 5, 0, 0);
		// convert to gray scale (required by Canny edge detector)
		cvCvtColor(image_rgb, image_gray, CV_RGB2GRAY);
		// detect edges
		cvCanny(image_gray, image_edges, CANNY_LOW, CANNY_HIGH, 3);
		// blurring edges improves contour detection
		cvSmooth(image_edges, image_edges, CV_GAUSSIAN, 5, 5, 0, 0);
		// detect contours from edges
		quad_coord lights_rgb[4];
		quad_coord_clear(lights_rgb);
//		fprintf(stdout, "RGB Contours (X,Y)\n");
		cvCopy( detect_contours(image_edges, lights_rgb), rgb_contours, NULL);

		/*
		 * Find valid landmark by matching contours
		 * Potential matching can be done using distance, area, overlap, etc...
		 */
		for (i = 0; i < LANDMARK_COUNT_MAX; ++i)
		{
			if ( QC_VALID == lights_rgb[i].valid &&
				 QC_VALID == lights_depth[i].valid )
			{
				CvPoint centroid_rgb = findCentroid( lights_rgb[i]);
				draw_value( rgb_contours, -1.0, centroid_rgb);
				CvPoint centroid_depth = findCentroid( lights_depth[i]);
				draw_value( image_disparity, i, centroid_depth);
				//int distance =
						distance2f(centroid_rgb, centroid_depth);
				// can return -1.0 indicating invalid/unknown
				// @TODO add an accept/reject condition
//				printf("Centroid #%d distance = %d\n", i, distance);
			}
		}

		/*
		 * Approximate depth to each landmark
		 */
		for (i = 0; i < LANDMARK_COUNT_MAX; ++i)
		{
			depths[i] = 0; // clear previous depth if any
			if ( QC_VALID == lights_depth[i].valid )
			{
				depths[i] = approximate_depth( image_disparity, lights_depth[i]);
				CvPoint centroid = findCentroid( lights_depth[i]);
				// will be centered about depth mask (may want to use center of RGB quad)
				draw_value( image_rgb, depths[i], centroid);
			}
		}

		/*
		 * Calculate relative position
		 */
		// TODO convert to array, turn in to vector for now
		cvSetZero(svrange);
		for (i = 0; i < LANDMARK_COUNT_MAX; ++i)
		{
			*( (float*)CV_MAT_ELEM_PTR( *svrange, i, 0 ) ) = depths[i];
			if ( 0 >= depths[i])
				sparse = true;
		}
		if ( !sparse)
		{
			printf( "%04.4f, %04.4f, %04.4f, %04.4f, ", depths[0], depths[1], depths[2], depths[3]);
			solve3D( svrange, svpos);
		}
		sparse = false;

		/*
		 * Display input and intermediate data for monitoring
		 */
		// blend live RGB and no depth images for monitoring purposes
		cvMerge( NULL, image_nodepth, NULL, NULL, image_nodepth_color);
		cvAddWeighted(image_rgb, 1.0, image_nodepth_color, 1.0, 0.0, image_blended);

		// write the coordinate and the disparity where user has left-clicked
		if (-1 != mouse_click.x && -1 != mouse_click.y)
		{
			int pixel_disparity = ((short *) image_disparity->imageData)[(mouse_click.y - y_offset) * 640 + (mouse_click.x - x_offset)];
			draw_value( image_blended, pixel_disparity, mouse_click);
		}

		cvShowImage( window_name_live, image_blended);
		cvShowImage( "Depth Mask", image_nodepth_mask);
		cvShowImage( "Disparity Contours", disparity_contours);
		//cvShowImage( "Edges", image_edges);
		cvShowImage("RGB Contours", rgb_contours);

		/*
		 * Wait for user input or timeout before continuing
		 */
		waitloop:
		key = cvWaitKey(1000/PROCESS_FPS);
		reinitialize = handle_key_input(key, &x_offset, &y_offset);

		if (reinitialize)
		{
			// initialize the mask
			cvZero(image_nodepth_mask);
			// clear flag
			reinitialize = false;
		}
	}

	/*
	 * Cleanup
	 */
	cvDestroyAllWindows();

	cvReleaseImage( &image_rgb);
	cvReleaseImage( &image_disparity);
	cvReleaseImage( &image_nodepth);
	cvReleaseImage( &image_nodepth_mask);
	cvReleaseImage( &image_nodepth_color);
	cvReleaseImage( &image_blended);
	cvReleaseImage( &disparity_contours);
	cvReleaseImage( &image_gray);
	cvReleaseImage( &image_edges);
	cvReleaseImage( &rgb_contours);

	cvReleaseMat(&user);
	cvReleaseMat(&svpos);
	cvReleaseMat(&svrange);

	// return the camera horizontal tilt
	tilt_horizontal();

	/*
	 * Exit
	 */
	return 0;
}
