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

#include "contour_functions.h"

vector<vector<Point>> legacyFindContours(Mat& Segmented)
{
	IplImage        SegmentedIpl = Segmented;
	CvMemStorage*   storage = cvCreateMemStorage(0);
	CvSeq*          contours = 0;

	cvFindContours(&SegmentedIpl, storage, &contours, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));

	vector<vector<Point>> result;
	for (; contours != 0; contours = contours->h_next)
	{
		vector<Point> contour_current;
		for (int i = 0; i < contours->total; ++i)
		{
			CvPoint* point = (CvPoint*)CV_GET_SEQ_ELEM(CvPoint, contours, i);
			Point pt = Point(point->x, point->y);
			contour_current.push_back(pt);
		}

		if (contour_current.size() > 0)
			result.push_back(contour_current);
	}

	cvReleaseMemStorage(&storage);
	return result;
}

void approximate_contour(vector<Point>& points, vector<Point>& points_approximated, int theta_threshold, int skip_count)
{
	if (points.size() == 0)
		return;

	points_approximated.push_back(points[0]);

	Point pt_old = Point(-1, 0);
	float angle_old = 9999;

	const int points_size = points.size();

	for (int i = 0; i < points_size; i += skip_count)
	{
		Point pt = points[i];

		if (pt_old.x != -1)
		{
			const float angle = get_angle(pt_old.x, pt_old.y, pt.x, pt.y);

			if (abs(angle - angle_old) > theta_threshold)
			{
				angle_old = angle;
				points_approximated.push_back(pt_old);
			}
		}
		pt_old = pt;
	}

	Point pt_end = points[points.size() - 1];
	Point pt_end_approximated = points_approximated[points_approximated.size() - 1];
	if (pt_end_approximated.x != pt_end.x || pt_end_approximated.y != pt_end.y)
		points_approximated.push_back(pt_end);
}

void midpoint_circle_push_pixel(int x, int y, int x_c, int y_c, vector<PointIndex>& result_out,
						        int& c00, int& c01, int& c10, int& c11, int& c20, int& c21, int& c30, int& c31)
{
	Point pt00 = Point(x_c - x, y_c - y);
	--c00;
	int index00 = c00 + 100000;
	Point pt01 = Point(x_c + x, y_c - y);
	++c01;
	int index01 = c01 + 100000;

	Point pt10 = Point(x_c + y, y_c - x);
	--c10;
	int index10 = c10 + 200000;
	Point pt11 = Point(x_c + y, y_c + x);
	++c11;
	int index11 = c11 + 200000;

	Point pt20 = Point(x_c - x, y_c + y);
	++c20;
	int index20 = c20 + 300000;
	Point pt21 = Point(x_c + x, y_c + y);
	--c21;
	int index21 = c21 + 300000;

	Point pt30 = Point(x_c - y, y_c - x);
	++c30;
	int index30 = c30 + 400000;
	Point pt31 = Point(x_c - y, y_c + x);
	--c31;
	int index31 = c31 + 400000;

	PointIndex pt_index00 = PointIndex(pt00, index00);
	PointIndex pt_index01 = PointIndex(pt01, index01);

	PointIndex pt_index10 = PointIndex(pt10, index10);
	PointIndex pt_index11 = PointIndex(pt11, index11);

	PointIndex pt_index20 = PointIndex(pt20, index20);
	PointIndex pt_index21 = PointIndex(pt21, index21);

	PointIndex pt_index30 = PointIndex(pt30, index30);
	PointIndex pt_index31 = PointIndex(pt31, index31);

	if(std::find(result_out.begin(), result_out.end(), pt_index00) == result_out.end())
		result_out.push_back(pt_index00);

	if(std::find(result_out.begin(), result_out.end(), pt_index01) == result_out.end())
		result_out.push_back(pt_index01);

	if(std::find(result_out.begin(), result_out.end(), pt_index10) == result_out.end())
		result_out.push_back(pt_index10);

	if(std::find(result_out.begin(), result_out.end(), pt_index11) == result_out.end())
		result_out.push_back(pt_index11);

	if(std::find(result_out.begin(), result_out.end(), pt_index20) == result_out.end())
		result_out.push_back(pt_index20);

	if(std::find(result_out.begin(), result_out.end(), pt_index21) == result_out.end())
		result_out.push_back(pt_index21);

	if(std::find(result_out.begin(), result_out.end(), pt_index30) == result_out.end())
		result_out.push_back(pt_index30);

	if(std::find(result_out.begin(), result_out.end(), pt_index31) == result_out.end())
		result_out.push_back(pt_index31);
}

void midpoint_circle(int x_in, int y_in, int radius_in, vector<Point>& result_out)
{
	vector<PointIndex> point_index_vec;

	int c00 = 0;
	int c01 = 0;
	int c10 = 0;
	int c11 = 0;
	int c20 = 0;
	int c21 = 0;
	int c30 = 0;
	int c31 = 0;

	int p = 1 - radius_in;
    int x = 0;
    int y = radius_in;
    midpoint_circle_push_pixel(x, y, x_in, y_in, point_index_vec, c00, c01, c10, c11, c20, c21, c30, c31);

    while (x <= y)
    {
        ++x;
        if (p < 0)
            p += 2 * x;
        else
        {
            p += 2 * (x - y);
            --y;
        }
        midpoint_circle_push_pixel(x, y, x_in, y_in, point_index_vec, c00, c01, c10, c11, c20, c21, c30, c31);
    }

    sort(point_index_vec.begin(), point_index_vec.end(), compare_point_index_index());
    for (PointIndex& pt_index : point_index_vec)
    	result_out.push_back(pt_index.pt);
}

void bresenham_line(int x1_in, int y1_in, int const x2_in, int const y2_in, vector<Point>& result_out, const uchar count_threshold)
{
	if (x1_in == x2_in && y1_in == y2_in)
	{
		result_out.push_back(Point(x1_in, y1_in));
		return;
	}

    int delta_x(x2_in - x1_in);
    signed char const ix((delta_x > 0) - (delta_x < 0));
    delta_x = abs(delta_x) << 1;
 
    int delta_y(y2_in - y1_in);
    signed char const iy((delta_y > 0) - (delta_y < 0));
    delta_y = abs(delta_y) << 1;
 
    result_out.push_back(Point(x1_in, y1_in));
 
    if (delta_x >= delta_y)
    {
        int error(delta_y - (delta_x >> 1));
 
        while (x1_in != x2_in)
        {
            if ((error >= 0) && (error || (ix > 0)))
            {
                error -= delta_x;
                y1_in += iy;
            }

            error += delta_y;
            x1_in += ix;
 			
 			result_out.push_back(Point(x1_in, y1_in));
    		if (result_out.size() == count_threshold || x1_in == 0 || y1_in == 0 || x1_in == WIDTH_SMALL_MINUS || y1_in == HEIGHT_SMALL_MINUS)
    			return;
        }
    }
    else
    {
        int error(delta_x - (delta_y >> 1));
 
        while (y1_in != y2_in)
        {
            if ((error >= 0) && (error || (iy > 0)))
            {
                error -= delta_y;
                x1_in += ix;
            }
            error += delta_x;
            y1_in += iy;
 
 			result_out.push_back(Point(x1_in, y1_in));
    		if (result_out.size() == count_threshold || x1_in == 0 || y1_in == 0 || x1_in == WIDTH_SMALL_MINUS || y1_in == HEIGHT_SMALL_MINUS)
    			return;
        }
    }
}

void extension_line(Point pt_start, Point pt_end, const uchar length, vector<Point>& line_points, const bool reverse)
{
	if (pt_start.y > pt_end.y)
	{
		Point pt_intersection;
		if (get_intersection_at_y(pt_end, pt_start, HEIGHT_SMALL, pt_intersection))
		{
			if (!reverse)
				bresenham_line(pt_start.x, pt_start.y, pt_intersection.x, pt_intersection.y, line_points, length);
			else
				bresenham_line(pt_end.x, pt_end.y, pt_intersection.x, pt_intersection.y, line_points, length);
		}
	}
	else if (pt_start.y < pt_end.y)
	{
		Point pt_intersection;
		if (get_intersection_at_y(pt_end, pt_start, 0, pt_intersection))
		{
			if (!reverse)
				bresenham_line(pt_start.x, pt_start.y, pt_intersection.x, pt_intersection.y, line_points, length);
			else
				bresenham_line(pt_end.x, pt_end.y, pt_intersection.x, pt_intersection.y, line_points, length);
		}
	}
	else if (pt_start.y == pt_end.y)
	{
		Point pt_intersection = Point(0, pt_start.y);
		if (pt_start.x < pt_end.x)
			pt_intersection.x = pt_start.x - length;
		else
			pt_intersection.x = pt_start.x + length;

		if (!reverse)
			bresenham_line(pt_start.x, pt_start.y, pt_intersection.x, pt_intersection.y, line_points, length);
		else
			bresenham_line(pt_end.x, pt_end.y, pt_intersection.x, pt_intersection.y, line_points, length);
	}
}

float get_min_dist(vector<Point>& pt_vec, Point& pt, bool accurate, Point* pt_dist_min)
{
	float dist_min = 9999;
	Point pt_dist_min0;
	for (Point& pt0 : pt_vec)
	{
		float dist = get_distance(pt0, pt, accurate);
		if (dist < dist_min)
		{
			pt_dist_min0 = pt0;
			dist_min = dist;
		}
	}
	if (pt_dist_min != NULL)
		*pt_dist_min = pt_dist_min0;

	return dist_min;
}

float get_max_dist(vector<Point>& pt_vec, Point& pt, bool accurate, Point* pt_dist_max)
{
	float dist_max = -1;
	Point pt_dist_max0;
	for (Point& pt0 : pt_vec)
	{
		float dist = get_distance(pt0, pt, accurate);
		if (dist > dist_max)
		{
			pt_dist_max0 = pt0;
			dist_max = dist;
		}
	}
	if (pt_dist_max != NULL)
		*pt_dist_max = pt_dist_max0;

	return dist_max;
}

Point get_y_min_point(vector<Point>& pt_vec)
{
	Point pt_y_min = Point(0, 9999);
	for (Point& pt : pt_vec)
		if (pt.y < pt_y_min.y)
			pt_y_min = pt;

	return pt_y_min;
}

Point get_y_max_point(vector<Point>& pt_vec)
{
	Point pt_y_max = Point(0, -1);
	for (Point& pt : pt_vec)
		if (pt.y > pt_y_max.y)
			pt_y_max = pt;

	return pt_y_max;
}

Point get_x_min_point(vector<Point>& pt_vec)
{
	Point pt_x_min = Point(9999, 0);
	for (Point& pt : pt_vec)
		if (pt.x < pt_x_min.x)
			pt_x_min = pt;

	return pt_x_min;
}

Point get_x_max_point(vector<Point>& pt_vec)
{
	Point pt_x_max = Point(-1, 0);
	for (Point& pt : pt_vec)
		if (pt.x > pt_x_max.x)
			pt_x_max = pt;

	return pt_x_max;
}

void get_bounds(vector<Point>& pt_vec, int& x_min, int& x_max, int& y_min, int& y_max)
{
	x_min = 9999;
	x_max = -1;
	y_min = 9999;
	y_max = -1;

	for (Point& pt : pt_vec)
	{
		if (pt.x > x_max)
			x_max = pt.x;
		if (pt.x < x_min)
			x_min = pt.x;
		if (pt.y < y_min)
			y_min = pt.y;
		if (pt.y > y_max)
			y_max = pt.y;
	}
}

bool check_bounds_small(Point& pt)
{
	if (pt.x < 0 || pt.x >= WIDTH_SMALL || pt.y < 0 || pt.y >= HEIGHT_SMALL)
		return false;

	return true;
}

void draw_contour(vector<Point>& contour, Mat& image, uchar gray, uchar thickness, int x_offset)
{
	Point pt_old = Point(-1, -1);
	for (Point pt : contour)
	{
		pt.x += x_offset;
		if (pt_old.x != -1)
			line(image, pt, pt_old, Scalar(gray), thickness);

		pt_old = pt;
	}
}

void sort_contour(vector<Point>& points, vector<Point>& points_sorted, Point& pivot)
{
	float dist_min = 9999;
	int index_dist_min;

	int index = 0;
	for (Point& pt : points)
	{
		float dist = get_distance(pt, pivot, false);
		if (dist < dist_min /*&& pt.x < pivot.x*/)
		{
			dist_min = dist;
			index_dist_min = index;
		}
		++index;
	}

	if (dist_min == 9999)
		return;

	const int points_size = points.size();
	for (int i = index_dist_min; i < points_size + index_dist_min; ++i)
	{
		int index_current = i;
		if (index_current >= points_size)
			index_current -= points_size;

		Point pt_current = points[index_current];
		points_sorted.push_back(pt_current);
	}
}