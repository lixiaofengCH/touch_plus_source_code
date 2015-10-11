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

#include "dtw.h"

vector<Point> preprocess_points(vector<Point>& vec)
{
	Point pivot = vec[vec.size() - 1];
	Point pt_y_max = get_y_max_point(vec);

	int x_diff = WIDTH_SMALL / 2 - pivot.x;
	int y_diff = HEIGHT_SMALL / 2 - pt_y_max.y;

	vector<Point> vec_adjusted;
	for (Point& pt : vec)
		vec_adjusted.push_back(Point(pt.x + x_diff, pt.y + y_diff));

	return vec_adjusted;
}

Mat compute_cost_mat(vector<Point>& vec0, vector<Point>& vec1, bool stereo)
{
	const int vec0_size_minus = vec0.size() - 1;
	const int vec1_size_minus = vec1.size() - 1;
	Mat cost_mat = Mat(vec1_size_minus, vec0_size_minus, CV_32FC1);

	if (stereo)
	{
		vector<Point> vec0_adjusted = preprocess_points(vec0);
		vector<Point> vec1_adjusted = preprocess_points(vec1);

		for (int i = 0; i < vec0_size_minus; ++i)
		{
			Point pt0_raw = vec0_adjusted[i];
			for (int j = 0; j < vec1_size_minus; ++j)
			{
				Point pt1_raw = vec1_adjusted[j];
				float dist = pow(abs(pt1_raw.y - pt0_raw.y), 2) + abs(pt1_raw.x - pt0_raw.x);
				cost_mat.ptr<float>(j, i)[0] = dist;
			}
		}
	}
	else
	{
		Point pivot0 = vec0[vec0.size() - 1];
		Point pivot1 = vec1[vec1.size() - 1];

		vector<Point> vec0_unwrapped;
		compute_unwrap2(vec0, pivot0, vec0_unwrapped);

		vector<Point> vec1_unwrapped;
		compute_unwrap2(vec1, pivot1, vec1_unwrapped);

		for (int i = 0; i < vec0_size_minus; ++i)
		{
			Point pt0 = vec0_unwrapped[i];
			for (int j = 0; j < vec1_size_minus; ++j)
			{
				Point pt1 = vec1_unwrapped[j];
				float dist = abs(pt1.y - pt0.y) + abs(pt1.x - pt0.x);
				cost_mat.ptr<float>(j, i)[0] = dist;
			}
		}
	}

	return cost_mat;
}

float compute_dtw(Mat cost_mat)
{
	if (cost_mat.cols == 0 || cost_mat.rows == 0)
		return FLT_MAX;

	const int i_max = cost_mat.cols;
	const int j_max = cost_mat.rows;
	
	for (int i = 0; i < i_max; ++i)
		for (int j = 0; j < j_max; ++j)
		{
			float val0;
			if (i - 1 < 0)
				val0 = FLT_MAX;
			else
				val0 = cost_mat.ptr<float>(j, i - 1)[0];

			float val1;
			if (i - 1 < 0 || j - i < 0)
				val1 = FLT_MAX;
			else
				val1 = cost_mat.ptr<float>(j - 1, i - 1)[0];

			float val2;
			if (j - 1 < 0)
				val2 = FLT_MAX;
			else
				val2 = cost_mat.ptr<float>(j - 1, i)[0];

			float val_min = std::min(std::min(val0, val1), val2);

			if (val_min == FLT_MAX)
				continue;
			
			cost_mat.ptr<float>(j, i)[0] += val_min;
		}

	return cost_mat.ptr<float>(j_max - 1, i_max - 1)[0];
}

vector<Point> compute_dtw_indexes(Mat cost_mat)
{
	vector<Point> seed_vec;
	if (cost_mat.cols == 0 || cost_mat.rows == 0)
		return seed_vec;

	const int i_max = cost_mat.cols;
	const int j_max = cost_mat.rows;
	
	for (int i = 0; i < i_max; ++i)
		for (int j = 0; j < j_max; ++j)
		{
			float val0;
			if (i - 1 < 0)
				val0 = FLT_MAX;
			else
				val0 = cost_mat.ptr<float>(j, i - 1)[0];

			float val1;
			if (i - 1 < 0 || j - i < 0)
				val1 = FLT_MAX;
			else
				val1 = cost_mat.ptr<float>(j - 1, i - 1)[0];

			float val2;
			if (j - 1 < 0)
				val2 = FLT_MAX;
			else
				val2 = cost_mat.ptr<float>(j - 1, i)[0];

			float val_min = std::min(std::min(val0, val1), val2);

			if (val_min == FLT_MAX)
				continue;
			
			cost_mat.ptr<float>(j, i)[0] += val_min;
		}

	Point seed = Point(i_max - 1, j_max - 1);
	seed_vec.push_back(seed);

	while (!(seed.x == 0 && seed.y == 0))
	{
		Point seed0 = Point(seed.x - 1, seed.y);
		Point seed1 = Point(seed.x - 1, seed.y - 1);
		Point seed2 = Point(seed.x, seed.y - 1);

		float seed0_gray;
		if (seed0.x >= 0 && seed0.y >= 0)
			seed0_gray = cost_mat.ptr<float>(seed0.y, seed0.x)[0];
		else
			seed0_gray = FLT_MAX;

		float seed1_gray;
		if (seed1.x >= 0 && seed1.y >= 0)
			seed1_gray = cost_mat.ptr<float>(seed1.y, seed1.x)[0];
		else
			seed1_gray = FLT_MAX;

		float seed2_gray;
		if (seed2.x >= 0 && seed2.y >= 0)
			seed2_gray = cost_mat.ptr<float>(seed2.y, seed2.x)[0];
		else
			seed2_gray = FLT_MAX;

		float seed_gray_min = min(min(seed0_gray, seed1_gray), seed2_gray);
		if (seed_gray_min == seed0_gray)
			seed = seed0;
		else if (seed_gray_min == seed1_gray)
			seed = seed1;
		else if (seed_gray_min == seed2_gray)
			seed = seed2;

		seed_vec.push_back(seed);
	}

	return seed_vec;	//vec1[seed.y] vec0[seed.x]
}