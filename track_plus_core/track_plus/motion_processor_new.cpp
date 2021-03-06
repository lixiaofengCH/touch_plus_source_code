/*
 * Touch+ Software
 * Copyright (C) 2015
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the Aladdin Free Public License as
 * published by the Aladdin Enterprises, either version 9 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Aladdin Free Public License for more details.
 *
 * You should have received a copy of the Aladdin Free Public License
 * along with this program.  If not, see <http://ghostscript.com/doc/8.54/Public.htm>.
 */

#include "motion_processor_new.h"
#include "mat_functions.h"
#include "contour_functions.h"
#include "console_log.h"
#include "camera_initializer_new.h"

float subtraction_threshold_ratio = 0.20;
int gray_threshold_range = 20;
float alpha = 1;

bool MotionProcessorNew::compute(Mat& image_in,             Mat& image_raw,  const int y_ref, float pitch,
								 bool construct_background, string name,     bool visualize)
{
	if (value_store.get_bool("first_pass", false) == false)
	{
		value_store.set_bool("first_pass", true);
		algo_name += name;
	}

	//------------------------------------------------------------------------------------------------------------------------

	Mat image_small;
	resize(image_in, image_small, Size(WIDTH_SMALL_HALF, HEIGHT_SMALL_HALF), 0, 0, INTER_LINEAR);
	Mat image_background_small = value_store.get_mat("image_background_small", true);

	Mat image_subtraction_small = Mat::zeros(HEIGHT_SMALL_HALF, WIDTH_SMALL_HALF, CV_8UC1);
	uchar diff_max_small = 0;

	for (int i = 0; i < WIDTH_SMALL_HALF; ++i)
		for (int j = 0; j < HEIGHT_SMALL_HALF; ++j)
		{
			uchar diff = abs(image_small.ptr<uchar>(j, i)[0] - image_background_small.ptr<uchar>(j, i)[0]);
			if (diff > diff_max_small)
				diff_max_small = diff;

			image_subtraction_small.ptr<uchar>(j, i)[0] = diff;
		}

	value_store.set_mat("image_background_small", image_small);
	threshold(image_subtraction_small, image_subtraction_small, diff_max_small * subtraction_threshold_ratio, 254, THRESH_BINARY);

	BlobDetectorNew* blob_detector_image_subtraction_small = value_store.get_blob_detector("blob_detector_image_subtraction_small");
	blob_detector_image_subtraction_small->compute(image_subtraction_small, 254, 0, WIDTH_SMALL_HALF, 0, HEIGHT_SMALL_HALF, true);

	if (blob_detector_image_subtraction_small->blobs->size() > 50)
	{
		bool ret_val = value_store.get_bool("result", false);
		if (ret_val)
			algo_name_vec.push_back(algo_name);

		return ret_val;
	}
	else
	{
		int count_total = 0;
		for (BlobNew& blob : *blob_detector_image_subtraction_small->blobs)
			count_total += blob.count;

		float faulty_image_ratio = (float)count_total / HEIGHT_SMALL_HALF / WIDTH_SMALL_HALF;

		if (faulty_image_ratio > 0.25)
			return false;
	}

	//------------------------------------------------------------------------------------------------------------------------

	image_ptr = image_in;

	LowPassFilter* low_pass_filter = value_store.get_low_pass_filter("low_pass_filter");

	bool both_moving_temp = false;
	both_moving = false;

	if (compute_background_static == false && construct_background == true)
	{
		value_store.reset();
		low_pass_filter->reset();

		gray_threshold_left = 9999;
		gray_threshold_right = 9999;
		diff_threshold = 9999;

		left_moving = false;
		right_moving = false;
	}

	compute_background_static = construct_background;

	left_moving = false;
	right_moving = false;

	//------------------------------------------------------------------------------------------------------------------------

	int current_frame = value_store.get_int("current_frame", 0);

	++current_frame;
	will_compute_next_frame = current_frame == target_frame - 1;

	bool to_return = false;
	if (current_frame < target_frame)
		to_return = true;
	else if (current_frame == target_frame + 1)
		current_frame = 0;

	value_store.set_int("current_frame", current_frame);

	if (to_return)
	{
		bool ret_val = value_store.get_bool("result", false);
		if (ret_val)
			algo_name_vec.push_back(algo_name);

		return ret_val;
	}

	//------------------------------------------------------------------------------------------------------------------------

	Mat image_background_unbiased = value_store.get_mat("image_background_unbiased", true);
	Mat image_subtraction_unbiased = Mat::zeros(HEIGHT_SMALL, WIDTH_SMALL, CV_8UC1);

	uchar diff_max_unbiased = 0;
	for (int i = 0; i < WIDTH_SMALL; ++i)
		for (int j = 0; j < y_ref; ++j)
		{
			uchar diff = abs(image_in.ptr<uchar>(j, i)[0] - image_background_unbiased.ptr<uchar>(j, i)[0]);
			if (diff > diff_max_unbiased)
				diff_max_unbiased = diff;

			image_subtraction_unbiased.ptr<uchar>(j, i)[0] = diff;
		}

	if (diff_max_unbiased < 32)
	{
		bool ret_val = value_store.get_bool("result", false);
		if (ret_val)
			algo_name_vec.push_back(algo_name);

		return ret_val;
	}

	threshold(image_subtraction_unbiased, image_subtraction_unbiased, diff_max_unbiased * subtraction_threshold_ratio, 254, THRESH_BINARY);

	value_store.set_mat("image_background_unbiased", image_in);

	//------------------------------------------------------------------------------------------------------------------------

	int entropy_left = 0;
	int entropy_right = 0;

	BlobDetectorNew* blob_detector_image_subtraction_unbiased = value_store.get_blob_detector("blob_detector_image_subtraction_unbiased");
	blob_detector_image_subtraction_unbiased->compute(image_subtraction_unbiased, 254, 0, WIDTH_SMALL, 0, HEIGHT_SMALL, true);

	for (BlobNew& blob : *blob_detector_image_subtraction_unbiased->blobs)
		if (blob.x < x_separator_middle)
			entropy_left += blob.count;
		else
			entropy_right += blob.count;

	int entropy_left_biased = 1;
	int entropy_right_biased = 1;

	BlobDetectorNew* blob_detector_image_subtraction = value_store.get_blob_detector("blob_detector_image_subtraction");
	for (BlobNew& blob : *blob_detector_image_subtraction->blobs)
		if (blob.x < x_separator_middle)
			entropy_left_biased += blob.count;
		else
			entropy_right_biased += blob.count;

	//------------------------------------------------------------------------------------------------------------------------

	if (entropy_left + entropy_right < (entropy_threshold * 8) && blob_detector_image_subtraction_unbiased->blobs->size() < 50)
	{
		int x_min = blob_detector_image_subtraction_unbiased->x_min_result;
		int x_max = blob_detector_image_subtraction_unbiased->x_max_result;
		int y_min = blob_detector_image_subtraction_unbiased->y_min_result;
		int y_max = blob_detector_image_subtraction_unbiased->y_max_result;

		float x_seed_vec0_max = value_store.get_float("x_seed_vec0_max");
		float x_seed_vec1_min = value_store.get_float("x_seed_vec1_min");

		int intensity_array[WIDTH_SMALL] { 0 };
		for (BlobNew& blob : *blob_detector_image_subtraction_unbiased->blobs)
			for (Point& pt : blob.data)
				++intensity_array[pt.x];

		vector<Point> hist_pt_vec;
		for (int i = 0; i < WIDTH_SMALL; ++i)
		{
			int j = intensity_array[i];
			low_pass_filter->compute(j, 0.5, "histogram_j");

			if (j < 10)
				continue;

			for (int j_current = 0; j_current < j; ++j_current)
				hist_pt_vec.push_back(Point(i, j_current));
		}

		Point seed0 = Point(x_min, 0);
		Point seed1 = Point(x_max, 0);

		vector<Point> seed_vec0;
		vector<Point> seed_vec1;

		while (true)
		{
			seed_vec0.clear();
			seed_vec1.clear();

			for (Point& pt : hist_pt_vec)
			{
				float dist0 = get_distance(pt, seed0, false);
				float dist1 = get_distance(pt, seed1, false);

				if (dist0 < dist1)
					seed_vec0.push_back(pt);
				else
					seed_vec1.push_back(pt);
			}

			if (seed_vec0.size() == 0 || seed_vec1.size() == 0)
				break;

			Point seed0_new = Point(0, 0);
			for (Point& pt : seed_vec0)
			{
				seed0_new.x += pt.x;
				seed0_new.y += pt.y;
			}
			seed0_new.x /= seed_vec0.size();
			seed0_new.y /= seed_vec0.size();

			Point seed1_new = Point(0, 0);
			for (Point& pt : seed_vec1)
			{
				seed1_new.x += pt.x;
				seed1_new.y += pt.y;
			}
			seed1_new.x /= seed_vec1.size();
			seed1_new.y /= seed_vec1.size();

			if (seed0 == seed0_new && seed1 == seed1_new)
				break;

			seed0 = seed0_new;
			seed1 = seed1_new;
		}

		if (seed_vec0.size() > 0 && seed_vec1.size() > 0)
		{
			x_seed_vec0_max = seed_vec0[seed_vec0.size() - 1].x;
			x_seed_vec1_min = seed_vec1[0].x;

			low_pass_filter->compute_if_smaller(x_seed_vec0_max, 0.5, "x_seed_vec0_max");
			low_pass_filter->compute_if_larger(x_seed_vec1_min, 0.5, "x_seed_vec1_min");

			value_store.set_float("x_seed_vec0_max", x_seed_vec0_max);
			value_store.set_float("x_seed_vec1_min", x_seed_vec1_min);

			value_store.set_bool("x_min_max_set", true);

			#if 0
			{
				Mat image_histogram = Mat::zeros(HEIGHT_SMALL, WIDTH_SMALL, CV_8UC1);

				for (Point& pt : seed_vec0)
					line(image_histogram, pt, Point(pt.x, 0), Scalar(127), 1);

				for (Point& pt : seed_vec1)
					line(image_histogram, pt, Point(pt.x, 0), Scalar(254), 1);

				circle(image_histogram, seed0, 5, Scalar(64), -1);
				circle(image_histogram, seed1, 5, Scalar(64), -1);

				line(image_histogram, Point(x_seed_vec0_max, 0), Point(x_seed_vec0_max, 9999), Scalar(64), 1);
				line(image_histogram, Point(x_seed_vec1_min, 0), Point(x_seed_vec1_min, 9999), Scalar(64), 1);

				imshow("image_histogramasd" + name, image_histogram);
			}
			#endif

			float subject_width0 = x_seed_vec0_max - x_min;
			float subject_width1 = x_max - x_seed_vec1_min;
			float subject_width_max = max(subject_width0, subject_width1);
			float gap_width = x_seed_vec1_min - x_seed_vec0_max;
			float total_width = x_max - x_min;
			float gap_ratio = gap_width / subject_width_max;

			float gap_size = x_seed_vec1_min - x_seed_vec0_max;
			low_pass_filter->compute_if_larger(gap_size, 0.1, "gap_size");

			int width0 = x_seed_vec0_max - x_min;
			int width1 = x_max - x_seed_vec1_min;
			int width_min = min(width0, width1);

			int entropy_min = min(entropy_left, entropy_right) + 1;
			int entropy_max = max(entropy_left, entropy_right) + 1;

			bool bool0 = ((float)(x_max - x_min) / (y_max - y_min)) > 1;
			bool bool1 = entropy_max / entropy_min == 1;
			bool bool2 =  gap_ratio > 0.3;
			bool bool3 = width_min >= 20;
			bool bool4 = gap_size >= 5;
			bool bool5 = x_seed_vec0_max < x_seed_vec1_min;

			both_moving_temp = bool0 && bool1 && bool2 && bool3 && bool4 && bool5;

			if (both_moving_temp)
			{
				float entropy_max = max(entropy_left, entropy_right);
				float entropy_min = min(entropy_left, entropy_right);

				if (entropy_max / entropy_min > 2/* || entropy_min < entropy_threshold*/)
					both_moving_temp = false;
			}
		}

		static bool both_moving0 = false;
		static bool both_moving1 = false;
		static bool both_moving_old0 = false;
		static bool both_moving_old1 = false;

		if (name == "0")
		{
			both_moving_old0 = both_moving0;
			both_moving0 = both_moving_temp;
		}
		else if (name == "1")
		{
			both_moving_old1 = both_moving1;
			both_moving1 = both_moving_temp;
		}

		if (both_moving0 || both_moving_old0 || both_moving1 || both_moving_old1)
			both_moving = true;

		static bool both_moving_0_set = false;
		static bool both_moving_1_set = false;

		if (both_moving)
		{
			left_moving = true;
			right_moving = true;

			//------------------------------------------------------------------------------------------------------------------------

			if (compute_x_separator_middle)
			{
				vector<int>* x_separator_middle_vec = value_store.get_int_vec("x_separator_middle_vec");
				if (x_separator_middle_vec->size() < 1000)
				{
					x_separator_middle_vec->push_back((x_seed_vec1_min + x_seed_vec0_max) / 2);
					sort(x_separator_middle_vec->begin(), x_separator_middle_vec->end());
				}
				x_separator_middle = (*x_separator_middle_vec)[x_separator_middle_vec->size() / 2];

				x_separator_middle_median = x_separator_middle;
				value_accumulator.compute(x_separator_middle_median, "x_separator_middle_median", 1000, WIDTH_SMALL_HALF, 0.5, true);
			}

			//------------------------------------------------------------------------------------------------------------------------

			if (both_moving_0_set == false || both_moving_1_set == false)
			{
				if (name == "0")
					both_moving_0_set = true;
				if (name == "1")
					both_moving_1_set = true;
			}
		}
		else if (entropy_left > entropy_threshold || entropy_right > entropy_threshold)
		{
			if (entropy_left > entropy_right)
				left_moving = true;
			else
				right_moving = true;
		}

		//------------------------------------------------------------------------------------------------------------------------

		if (both_moving_0_set && both_moving_1_set)
		{
			Mat image_background = value_store.get_mat("image_background", true);
			Mat image_subtraction = Mat::zeros(HEIGHT_SMALL, WIDTH_SMALL, CV_8UC1);

			uchar diff_max = 0;
			for (int i = 0; i < WIDTH_SMALL; ++i)
				for (int j = 0; j < y_ref; ++j)
				{
					uchar diff = abs(image_in.ptr<uchar>(j, i)[0] - image_background.ptr<uchar>(j, i)[0]);
					if (diff > diff_max)
						diff_max = diff;

					image_subtraction.ptr<uchar>(j, i)[0] = diff;
				}
			threshold(image_subtraction, image_subtraction, diff_max * subtraction_threshold_ratio, 254, THRESH_BINARY);
			// GaussianBlur(image_subtraction, image_subtraction, Size(9, 9), 0, 0);
			// threshold(image_subtraction, image_subtraction, 100, 254, THRESH_BINARY);

			//------------------------------------------------------------------------------------------------------------------------

			if (right_moving && entropy_right < (entropy_threshold * 4))
				if (entropy_right / entropy_right_biased == 0)
					for (int i = x_separator_middle; i < WIDTH_SMALL; ++i)
						for (int j = 0; j < HEIGHT_SMALL; ++j)
							image_background.ptr<uchar>(j, i)[0] +=
								(image_in.ptr<uchar>(j, i)[0] - image_background.ptr<uchar>(j, i)[0]) * alpha;

			if (left_moving && entropy_left < (entropy_threshold * 4))
				if (entropy_left / entropy_left_biased == 0)
					for (int i = 0; i < x_separator_middle; ++i)
						for (int j = 0; j < HEIGHT_SMALL; ++j)
							image_background.ptr<uchar>(j, i)[0] +=
								(image_in.ptr<uchar>(j, i)[0] - image_background.ptr<uchar>(j, i)[0]) * alpha;

			value_store.set_mat("image_background", image_background);

			//------------------------------------------------------------------------------------------------------------------------

			blob_detector_image_subtraction->compute(image_subtraction, 254, 0, WIDTH_SMALL, 0, HEIGHT_SMALL, true);

			if ((left_moving || right_moving) && value_store.get_bool("x_min_max_set"))
			{
				if (both_moving)
				{
					x_separator_left = blob_detector_image_subtraction->x_min_result;
					x_separator_right = blob_detector_image_subtraction->x_max_result;

					x_separator_left_median = x_separator_left;
					x_separator_right_median = x_separator_right;
					value_accumulator.compute(x_separator_left_median, "x_separator_left_median", 1000, 0, 0.99, true);
					value_accumulator.compute(x_separator_right_median, "x_separator_right_median", 1000, WIDTH_SMALL, 0.01, true);
				}
				else if (left_moving)
					x_separator_left = blob_detector_image_subtraction->x_min_result;
				else if (right_moving)
					x_separator_right = blob_detector_image_subtraction->x_max_result;
			}

			//------------------------------------------------------------------------------------------------------------------------

			int intensity_array0[HEIGHT_SMALL] { 0 };
			for (BlobNew& blob : *blob_detector_image_subtraction->blobs)//mark
				for (Point& pt : blob.data)
					++intensity_array0[pt.y];

			int y_min = 9999;
			int y_max = 0;

			vector<Point> hist_pt_vec0;
			for (int i = 0; i < HEIGHT_SMALL; ++i)
			{
				int j = intensity_array0[i];
				low_pass_filter->compute(j, 0.5, "histogram_j");

				if (j < 10)
					continue;

				for (int j_current = 0; j_current < j; ++j_current)
					hist_pt_vec0.push_back(Point(j_current, i));

				if (i < y_min)
					y_min = i;
				if (i > y_max)
					y_max = i;
			}

			Mat image_histogram = Mat::zeros(HEIGHT_SMALL, WIDTH_SMALL, CV_8UC1);
			for (Point& pt : hist_pt_vec0)
				line(image_histogram, pt, Point(0, pt.y), Scalar(254), 1);

			BlobDetectorNew* blob_detector_image_histogram = value_store.get_blob_detector("blob_detector_image_histogram");
			blob_detector_image_histogram->compute(image_histogram, 254, 0, WIDTH_SMALL, 0, HEIGHT_SMALL, true);

			if (both_moving)
			{
				y_separator_down = blob_detector_image_histogram->blob_max_size->y_max;
				value_store.set_int("y_separator_down", y_separator_down);

				y_separator_down_median = y_separator_down;
				value_accumulator.compute(y_separator_down_median, "y_separator_down_median", 1000, HEIGHT_SMALL_MINUS, 0.01, true);
			}
			else if (left_moving || right_moving)
			{
				y_separator_down = value_store.get_int("y_separator_down");
				int y_separator_down_new = blob_detector_image_histogram->blob_max_size->y_max;
				if (y_separator_down_new > y_separator_down)
					y_separator_down = y_separator_down_new;
			}

			//------------------------------------------------------------------------------------------------------------------------

			if ((((left_moving || right_moving) && value_store.get_bool("result", true)) || both_moving))
			{
				if (both_moving)
				{
					int blobs_y_min = 9999;
					for (BlobNew& blob : *blob_detector_image_subtraction->blobs)
						if (blob.y_min < blobs_y_min)
							blobs_y_min = blob.y_min;

					y_separator_up = blobs_y_min + 10;

					y_separator_up_median = y_separator_up;
					value_accumulator.compute(y_separator_up_median, "y_separator_up_median", 1000, 0, 0.99, true);

					value_accumulator.compute(y_separator_up, "y_separator_up", 1000, 0, 0.99, true);
				}

				//------------------------------------------------------------------------------------------------------------------------

				Point pt_intersection_down_left = value_store.get_point("pt_intersection_down_left");
				Point pt_intersection_down_right = value_store.get_point("pt_intersection_down_right");
				Point pt_intersection_up_left = value_store.get_point("pt_intersection_up_left");
				Point pt_intersection_up_right = value_store.get_point("pt_intersection_up_right");

				//------------------------------------------------------------------------------------------------------------------------

				if (value_accumulator.ready && construct_background)
				{
					if (y_separator_up > 0 && !value_store.get_bool("triangle_fill_complete", false))
					{
						value_store.set_bool("triangle_fill_complete", true);

						Point pt_center = Point(x_separator_middle, y_separator_up);
						Mat image_flood_fill = Mat::zeros(pt_center.y + 1, WIDTH_SMALL, CV_8UC1);

						line(image_flood_fill, pt_center, Point(x_separator_left, 0), Scalar(254), 1);
						line(image_flood_fill, pt_center, Point(x_separator_right, 0), Scalar(254), 1);
						floodFill(image_flood_fill, Point(pt_center.x, 0), Scalar(254));

						const int j_max = image_flood_fill.rows;
						for (int i = 0; i < WIDTH_SMALL; ++i)
							for (int j = 0; j < j_max; ++j)
								if (image_flood_fill.ptr<uchar>(j, i)[0] > 0)
									fill_image_background_static(i, j, image_in); //triangle fill
					}

					for (int i = 0; i < WIDTH_SMALL; ++i)
						for (int j = y_separator_down; j < HEIGHT_SMALL; ++j)
							fill_image_background_static(i, j, image_in); //bottom fill

					//------------------------------------------------------------------------------------------------------------------------

					static float gray_threshold_left_stereo = 9999;
					static float gray_threshold_right_stereo = 9999;

					if (name == "0")
					{
						vector<uchar> gray_vec_left;
						vector<uchar> gray_vec_right;

						for (BlobNew& blob : *(blob_detector_image_subtraction->blobs))
							if (blob.active)
	                        {
								if (blob.x < x_separator_middle)
								{
									for (Point pt : blob.data)
									{
										uchar gray = std::max(image_in.ptr<uchar>(pt.y, pt.x)[0], image_background.ptr<uchar>(pt.y, pt.x)[0]);
										gray_vec_left.push_back(gray);
									}
								}
								else
								{
									for (Point pt : blob.data)
									{
										uchar gray = std::max(image_in.ptr<uchar>(pt.y, pt.x)[0], image_background.ptr<uchar>(pt.y, pt.x)[0]);
										gray_vec_right.push_back(gray);
									}	
								}
	                        }

						if (gray_vec_left.size() > 0 && left_moving)
						{
							sort(gray_vec_left.begin(), gray_vec_left.end());
							float gray_median_left = gray_vec_left[gray_vec_left.size() * 0.5];

							gray_threshold_left = gray_median_left - gray_threshold_range;
							low_pass_filter->compute(gray_threshold_left, 0.1, "gray_threshold_left");
							gray_threshold_left_stereo = gray_threshold_left;
						}

						if (gray_vec_right.size() > 0 && right_moving)
						{
							sort(gray_vec_right.begin(), gray_vec_right.end());
							float gray_median_right = gray_vec_right[gray_vec_right.size() * 0.5];

							gray_threshold_right = gray_median_right - gray_threshold_range;
							low_pass_filter->compute(gray_threshold_right, 0.1, "gray_threshold_right");
							gray_threshold_right_stereo = gray_threshold_right;
						}
					}
					else
					{
						gray_threshold_left = gray_threshold_left_stereo;
						gray_threshold_right = gray_threshold_right_stereo;
					}

					//------------------------------------------------------------------------------------------------------------------------

					Mat image_in_thresholded = Mat::zeros(HEIGHT_SMALL, WIDTH_SMALL, CV_8UC1);
					for (int i = 0; i < WIDTH_SMALL; ++i)
						for (int j = 0; j < HEIGHT_SMALL; ++j)
							if (i < x_separator_middle && image_in.ptr<uchar>(j, i)[0] > gray_threshold_left)
								image_in_thresholded.ptr<uchar>(j, i)[0] = 127;
							else if (i > x_separator_middle && image_in.ptr<uchar>(j, i)[0] > gray_threshold_right)
								image_in_thresholded.ptr<uchar>(j, i)[0] = 127;

					//------------------------------------------------------------------------------------------------------------------------

					uchar static_diff_max = 255;
					for (int i = 0; i < WIDTH_SMALL; ++i)
						for (int j = 0; j < HEIGHT_SMALL; ++j)
							if (image_in_thresholded.ptr<uchar>(j, i)[0] > 0)
								if (image_background_static.ptr<uchar>(j, i)[0] < 255)
								{
									const uchar diff = abs(image_in.ptr<uchar>(j, i)[0] - image_background_static.ptr<uchar>(j, i)[0]);
									if (diff > static_diff_max)
										static_diff_max = diff;
								}

					static float diff_threshold_stereo;
					if (name == "0")
					{
						diff_threshold = static_diff_max * 0.2;
						diff_threshold_stereo = diff_threshold;
					}
					else
						diff_threshold = diff_threshold_stereo;

					//------------------------------------------------------------------------------------------------------------------------

					Mat image_canny;
					Canny(image_raw, image_canny, 20, 60, 3);

					for (int i = 0; i < WIDTH_SMALL; ++i)
						for (int j = 0; j < HEIGHT_SMALL; ++j)
							if (image_canny.ptr<uchar>(j, i)[0] > 0)
								image_in_thresholded.ptr<uchar>(j, i)[0] = 0;

					for (BlobNew& blob : *(blob_detector_image_subtraction->blobs))
						for (Point& pt : blob.data)
							if (image_in_thresholded.ptr<uchar>(pt.y, pt.x)[0] == 127)
								floodFill(image_in_thresholded, pt, Scalar(254));

					dilate(image_in_thresholded, image_in_thresholded, Mat(), Point(-1, -1), 3);

					//------------------------------------------------------------------------------------------------------------------------

					Mat image_borders = value_store.get_mat("image_borders", true);
					bool image_borders_set = false;

					const int borders_x_offset = value_store.get_bool("image_borders_public_set", false) ? 10 : 20;

					while ((left_moving || right_moving) && value_accumulator.ready)
					{
						int x_separator_left_temp = x_separator_left - borders_x_offset;
						int x_separator_right_temp = x_separator_right + borders_x_offset;

						if (!value_store.get_bool("border_first_pass", false))
						{
							x_separator_left_temp = x_separator_left_median;
							x_separator_right_temp = x_separator_right_median;
							value_store.set_bool("border_first_pass", true);
						}

						float width_diff = cubic(pitch, 7.214493, 0.4996785, 0.0002563892, -0.00003657664) * 3 / 4;
						//float width_diff = quadratic(pitch, 7.755859, 0.5430825, -0.002945822);

						float width_diff_left = map_val(x_separator_left_temp, 0, WIDTH_SMALL / 2, -width_diff, 0);
						Point pt_left_up = Point(x_separator_left_temp - width_diff_left, y_separator_up);
						Point pt_left_down = Point(x_separator_left_temp, y_separator_down);

						float width_diff_right = map_val(x_separator_right_temp, WIDTH_SMALL_MINUS / 2, WIDTH_SMALL_MINUS, 0, width_diff);
						Point pt_right_up = Point(x_separator_right_temp - width_diff_right, y_separator_up);
						Point pt_right_down = Point(x_separator_right_temp, y_separator_down);

						bool b_left0 = get_intersection_at_y(pt_left_up.x, pt_left_up.y, 
														pt_left_down.x, pt_left_down.y, 0, 
														pt_intersection_up_left);

						bool b_left1 = get_intersection_at_y(pt_left_up.x, pt_left_up.y, 
														pt_left_down.x, pt_left_down.y, HEIGHT_SMALL_MINUS, 
														pt_intersection_down_left);

						bool b_right0 = get_intersection_at_y(pt_right_up.x, pt_right_up.y, 
														pt_right_down.x, pt_right_down.y, 0, 
														pt_intersection_up_right);

						bool b_right1 = get_intersection_at_y(pt_right_up.x, pt_right_up.y, 
														pt_right_down.x, pt_right_down.y, HEIGHT_SMALL_MINUS, 
														pt_intersection_down_right);

						image_borders = Mat::zeros(HEIGHT_SMALL, WIDTH_SMALL, CV_8UC1);

						if (b_left0 && b_left1 && left_moving)
						{
							line(image_borders, pt_intersection_up_left, pt_intersection_down_left, Scalar(254), 1);

							if (pt_intersection_up_left.x > 0)
								floodFill(image_borders, Point(0, 0), Scalar(254));

							image_borders_set = true;
						}

						if (b_right0 && b_right1 && right_moving)
						{
							line(image_borders, pt_intersection_up_right, pt_intersection_down_right, Scalar(254), 1);

							if (pt_intersection_up_right.x < WIDTH_SMALL_MINUS)
								floodFill(image_borders, Point(WIDTH_SMALL_MINUS, 0), Scalar(254));

							image_borders_set = true;
						}

						if (value_store.get_bool("result", false) && !value_store.get_bool("image_borders_public_set", false))
						{
							value_store.set_bool("image_borders_public_set", true);
							image_borders_public = image_borders.clone();
						}

						value_store.set_mat("image_borders", image_borders);
						break;
					}

					//------------------------------------------------------------------------------------------------------------------------

					BlobDetectorNew* blob_detector_image_in_thresholded =
						value_store.get_blob_detector("blob_detector_image_in_thresholded");
						
					blob_detector_image_in_thresholded->compute(image_in_thresholded, 127, 0, WIDTH_SMALL, 0, HEIGHT_SMALL, true);

					for (BlobNew& blob : *blob_detector_image_in_thresholded->blobs)
					{
						float overlap_count = 0;
						for (Point& pt : blob.data)
							if (image_borders.ptr<uchar>(pt.y, pt.x)[0] > 0)
								++overlap_count;

						const float overlap_ratio = overlap_count / blob.count;
						if (overlap_ratio > 0.5 || blob.x < x_separator_left || blob.x > x_separator_right || blob.y > y_separator_down)
							for (Point& pt : blob.data)
								fill_image_background_static(pt.x, pt.y, image_in); //out of bounds blob fill
					}

					//------------------------------------------------------------------------------------------------------------------------

					const int x_separator_middle_const = x_separator_middle;

					if (gray_threshold_left != 9999)
						for (int i = 0; i < x_separator_middle; ++i)
							for (int j = 0; j < HEIGHT_SMALL; ++j)
								if (image_in_thresholded.ptr<uchar>(j, i)[0] == 0)
									fill_image_background_static(i, j, image_in); //left dilated hand fill

					if (gray_threshold_right != 9999)
						for (int i = x_separator_middle; i < WIDTH_SMALL; ++i)
							for (int j = 0; j < HEIGHT_SMALL; ++j)
								if (image_in_thresholded.ptr<uchar>(j, i)[0] == 0)
									fill_image_background_static(i, j, image_in); //right dilated hand fill

					//------------------------------------------------------------------------------------------------------------------------

					if (image_borders_set)
					{
						for (int i = 0; i < WIDTH_SMALL; ++i)
							for (int j = 0; j < HEIGHT_SMALL; ++j)
								if (image_borders.ptr<uchar>(j, i)[0] > 0)
									fill_image_background_static(i, j, image_in); //borders fill
					}

					//------------------------------------------------------------------------------------------------------------------------

					if (both_moving && !value_store.get_bool("result", false))
					{
						float hole_count_left = 0;
						float hole_count_right = 0;

						int hole_x_min_left = 9999;
						int hole_x_max_left = -1;
						int hole_y_min_left = 9999;
						int hole_y_max_left = -1;

						int hole_x_min_right = 9999;
						int hole_x_max_right = -1;
						int hole_y_min_right = 9999;
						int hole_y_max_right = -1;

						for (int i = 0; i < WIDTH_SMALL; ++i)
							for (int j = 0; j < HEIGHT_SMALL; ++j)
								if (image_background_static.ptr<uchar>(j, i)[0] == 255)
								{
									if (i < x_separator_middle)
									{
										++hole_count_left;
										if (i < hole_x_min_left)
											hole_x_min_left = i;
										if (i > hole_x_max_left)
											hole_x_max_left = i;
										if (j < hole_y_min_left)
											hole_y_min_left = j;
										if (j > hole_y_max_left)
											hole_y_max_left = j;
									}
									else
									{
										++hole_count_right;
										if (i < hole_x_min_right)
											hole_x_min_right = i;
										if (i > hole_x_max_right)
											hole_x_max_right = i;
										if (j < hole_y_min_right)
											hole_y_min_right = j;
										if (j > hole_y_max_right)
											hole_y_max_right = j;
									}
								}

						float hole_width_left = hole_x_max_left - hole_x_min_left;
						float hole_height_left = hole_y_max_left - hole_y_min_left;

						float hole_width_right = hole_x_max_right - hole_x_min_right;
						float hole_height_right = hole_y_max_right - hole_y_min_right;

						int entropy_x_min_left = 9999;
						int entropy_x_max_left = -1;
						int entropy_y_min_left = 9999;
						int entropy_y_max_left = -1;

						int entropy_x_min_right = 9999;
						int entropy_x_max_right = -1;
						int entropy_y_min_right = 9999;
						int entropy_y_max_right = -1;

						for (BlobNew& blob : *blob_detector_image_subtraction_unbiased->blobs)
							for (Point& pt : blob.data)
							{
								int i = pt.x;
								int j = pt.y;

								if (i < x_separator_middle)
								{
									++hole_count_left;
									if (i < entropy_x_min_left)
										entropy_x_min_left = i;
									if (i > entropy_x_max_left)
										entropy_x_max_left = i;
									if (j < entropy_y_min_left)
										entropy_y_min_left = j;
									if (j > entropy_y_max_left)
										entropy_y_max_left = j;
								}
								else
								{
									++hole_count_right;
									if (i < entropy_x_min_right)
										entropy_x_min_right = i;
									if (i > entropy_x_max_right)
										entropy_x_max_right = i;
									if (j < entropy_y_min_right)
										entropy_y_min_right = j;
									if (j > entropy_y_max_right)
										entropy_y_max_right = j;
								}
							}

						float entropy_width_left = entropy_x_max_left - entropy_x_min_left;
						float entropy_height_left = entropy_y_max_left - entropy_y_min_left;

						float entropy_width_right = entropy_x_max_right - entropy_x_min_right;
						float entropy_height_right = entropy_y_max_right - entropy_y_min_right;

						float ratio0 = hole_count_right / (entropy_right + 0.1);
						float ratio1 = hole_count_left / (entropy_left + 0.1);
						float ratio2 = hole_width_right / (entropy_width_right + 0.1);
						float ratio3 = hole_width_left / (entropy_width_left + 0.1);
						float ratio4 = hole_height_right / (entropy_height_right + 0.1);
						float ratio5 = hole_height_left / (entropy_height_left + 0.1);

						// float ratio_max = max(max(max(max(max(ratio0, ratio1), ratio2), ratio3), ratio4), ratio5);
						float ratio_max = max(max(max(ratio2, ratio3), ratio4), ratio5);

						if (ratio_max < 2)
						{
							alpha = 0.10;
							value_store.set_bool("result", true);
						}
					}
				}

				//------------------------------------------------------------------------------------------------------------------------

				value_store.get_point("pt_intersection_down_left", pt_intersection_down_left);
				value_store.get_point("pt_intersection_down_right", pt_intersection_down_right);
				value_store.get_point("pt_intersection_up_left", pt_intersection_up_left);
				value_store.get_point("pt_intersection_up_right", pt_intersection_up_right);

				//------------------------------------------------------------------------------------------------------------------------

				if (enable_imshow && visualize)
				{
					Mat image_visualization = image_background_static.clone();

					line(image_visualization, Point(x_separator_left, 0), Point(x_separator_left, 999), Scalar(254), 1);
					line(image_visualization, Point(x_separator_right, 0), Point(x_separator_right, 999), Scalar(254), 1);
					line(image_visualization, Point(x_separator_middle, 0), Point(x_separator_middle, 999), Scalar(254), 1);
					line(image_visualization, Point(0, y_separator_down), Point(999, y_separator_down), Scalar(254), 1);
					line(image_visualization, Point(0, y_separator_up), Point(999, y_separator_up), Scalar(254), 1);
					line(image_visualization, Point(0, y_ref), Point(999, y_ref), Scalar(254), 1);

					line(image_visualization, pt_intersection_up_left, pt_intersection_down_left, Scalar(254), 1);
					line(image_visualization, pt_intersection_up_right, pt_intersection_down_right, Scalar(254), 1);

					imshow("image_visualizationadfasdfdfdfsff" + name, image_visualization);
					imshow("image_subtractionsdfsdfsdfsaddddf" + name, image_subtraction);
				}
			}
		}
	}
	bool ret_val = value_store.get_bool("result", false);
	if (ret_val)
		algo_name_vec.push_back(algo_name);

	return ret_val;
}

inline void MotionProcessorNew::fill_image_background_static(const int x, const int y, Mat& image_in)
{
	if (!compute_background_static)
		return;

	uchar* pix_ptr = &(image_background_static.ptr<uchar>(y, x)[0]);

	if (*pix_ptr == 255)
		*pix_ptr = image_in.ptr<uchar>(y, x)[0];
	else
		*pix_ptr = *pix_ptr + ((image_in.ptr<uchar>(y, x)[0] - *pix_ptr) * 0.1);
}