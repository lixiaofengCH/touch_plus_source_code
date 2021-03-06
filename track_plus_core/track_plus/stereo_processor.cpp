#include "stereo_processor.h"

struct BlobPairOverlap
{
	BlobNew blob0;
	BlobNew blob1;

	int overlap;
	int index0;
	int index1;
	int y_diff_diff;
	int overlap_real;

	Mat image0;
	Mat image1;

	string key;

	BlobPairOverlap(BlobNew* _blob0, BlobNew* _blob1, int _overlap, int _index0, int _index1,
						int _y_diff_diff, int _overlap_real, Mat& _image0, Mat& _image1)
	{
		blob0 = *_blob0;
		blob1 = *_blob1;
		overlap = _overlap;
		index0 = _index0;
		index1 = _index1;
		y_diff_diff = _y_diff_diff;
		overlap_real = _overlap_real;
		image0 = _image0;
		image1 = _image1;

		key = to_string(blob0.track_index) + ":" + to_string(blob1.track_index);
	};
};

struct compare_blob_pair_overlap
{
	bool operator() (const BlobPairOverlap& blob_pair0, const BlobPairOverlap& blob_pair1)
	{
		return (blob_pair0.overlap > blob_pair1.overlap);
	}
};

void StereoProcessor::compute(SCOPA& scopa0, SCOPA& scopa1,
							  PointResolver& point_resolver, PointerMapper& pointer_mapper, Mat& image0, Mat& image1, bool visualize)
{
	LowPassFilter* low_pass_filter = value_store.get_low_pass_filter("low_pass_filter");

	Point pt_y_max0 = get_y_max_point(scopa0.fingertip_points);
	Point pt_y_max1 = get_y_max_point(scopa1.fingertip_points);
	int alignment_y_diff = pt_y_max0.y - pt_y_max1.y;
	int alignment_x_diff = scopa0.pt_alignment.x - scopa1.pt_alignment.x;

	//------------------------------------------------------------------------------------------------------------------------

	vector<BlobPairOverlap> blob_pair_vec;

	int index0 = 0;
	for (BlobNew& blob0 : scopa0.fingertip_blobs)
	{
		int index1 = 0;
		for (BlobNew& blob1 : scopa1.fingertip_blobs)
		{
			float y_diff = blob0.y_max - blob1.y_max;
			float y_diff_diff = abs(y_diff - alignment_y_diff) + 1;
			float dist = get_distance(blob0.pt_tip, Point(blob1.pt_tip.x + alignment_x_diff, blob1.pt_tip.y + alignment_y_diff), true) + 1;
			float overlap_real = blob0.compute_overlap(blob1, alignment_x_diff, alignment_y_diff, 2);
			float overlap = overlap_real; /** 1000 / y_diff_diff / dist;*/
			blob_pair_vec.push_back(BlobPairOverlap(&blob0, &blob1, overlap, index0, index1, y_diff_diff, overlap_real, image0, image1));

			++index1;
		}
		++index0;
	}
	sort(blob_pair_vec.begin(), blob_pair_vec.end(), compare_blob_pair_overlap());

	//------------------------------------------------------------------------------------------------------------------------

	vector<BlobPairOverlap> blob_pair_vec_filtered;

	bool checker0[100] = { 0 };
	bool checker1[100] = { 0 };
	for (BlobPairOverlap& blob_pair : blob_pair_vec)
	{
		if (checker0[blob_pair.index0] == true || checker1[blob_pair.index1] == true)
			continue;

		checker0[blob_pair.index0] = true;
		checker1[blob_pair.index1] = true;

		if (blob_pair.y_diff_diff > 10 /*|| blob_pair.overlap_real == 0*/)
			continue;

		blob_pair_vec_filtered.push_back(blob_pair);
	}

	//------------------------------------------------------------------------------------------------------------------------

	Point pt_resolved_pivot0 = point_resolver.reprojector->remap_point(scopa0.pt_palm, 0, 4);
	Point pt_resolved_pivot1 = point_resolver.reprojector->remap_point(scopa1.pt_palm, 1, 4);

	Point3f pt3d_pivot = point_resolver.reprojector->reproject_to_3d(pt_resolved_pivot0.x, pt_resolved_pivot0.y,
															         pt_resolved_pivot1.x, pt_resolved_pivot1.y);

	Point3f pt3d_pivot_old = value_store.get_point3f("pt3d_pivot_old", pt3d_pivot);

	float z_new = pt3d_pivot.z;
	float z_old = pt3d_pivot_old.z;
	float z_max = max(z_new, z_old) + 1;
	float z_min = min(z_new, z_old) + 1;

	if (z_max / z_min >= 3)
		pt3d_pivot = pt3d_pivot_old;

	value_store.set_point3f("pt3d_pivot_old", pt3d_pivot);

	//------------------------------------------------------------------------------------------------------------------------

	static const int frame_cache_num = 3;
	static int cached_count = -1;
	static bool begin_action = false;

	++cached_count;
	if (cached_count == frame_cache_num)
	{
		cached_count = 0;
		begin_action = true;
	}

	static vector<BlobPairOverlap> stereo_pair_vec[frame_cache_num];
	stereo_pair_vec[cached_count] = blob_pair_vec_filtered;

	if (!begin_action)
		return;

	int index_before = cached_count - (frame_cache_num - 1);
	if (index_before < 0)
		index_before = frame_cache_num + index_before;

	vector<BlobPairOverlap> stereo_pair_current = stereo_pair_vec[index_before];
	vector<BlobPairOverlap> stereo_pair_latest = stereo_pair_vec[cached_count];

	//------------------------------------------------------------------------------------------------------------------------

	pt3d_vec.clear();
	confidence_vec.clear();
	float confidence_max = -1;

	Mat image_visualization;
	if (visualize)
		image_visualization = Mat::zeros(HEIGHT_LARGE, WIDTH_LARGE, CV_8UC1);

	for (BlobPairOverlap& blob_pair : stereo_pair_current)
	{
		bool found = false;
		for (BlobPairOverlap& blob_pair_latest : stereo_pair_latest)
			if (blob_pair.key == blob_pair_latest.key)
			{
				found = true;
				break;
			}
		if (!found)
			continue;

		BlobNew* blob0 = &blob_pair.blob0;
		BlobNew* blob1 = &blob_pair.blob1;

		if (!blob0->active || !blob1->active)
			continue;

		Point2f pt_resolved0 = point_resolver.compute(blob0->pt_tip, blob_pair.image0, 0);
		Point2f pt_resolved1 = point_resolver.compute(blob1->pt_tip, blob_pair.image1, 1);

		if (pt_resolved0.x == 9999 || pt_resolved1.x == 9999)
			continue;

		Point3f pt3d = point_resolver.reprojector->reproject_to_3d(pt_resolved0.x, pt_resolved0.y, pt_resolved1.x, pt_resolved1.y);
		low_pass_filter->compute(pt3d, 0.5, blob_pair.key);

		if (abs(pt3d.z - pt3d_pivot.z) >= 100)
			continue;

		pt3d_vec.push_back(pt3d);

		float confidence = min(blob0->count, blob1->count);
		if (confidence > confidence_max)
			confidence_max = confidence;

		confidence_vec.push_back(confidence);

		if (!visualize)
			continue;

		circle(image_visualization, Point(320 + pt3d.x, 240 + pt3d.y), pow(1000 / (pt3d.z + 1), 2), Scalar(254), 1);

#if 0
		circle(image_visualization, pt_resolved0, 5, Scalar(127), 2);
		circle(image_visualization, pt_resolved1, 5, Scalar(254), 2);
		circle(image_visualization, pt_resolved_pivot0, 10, Scalar(127), 2);
		circle(image_visualization, pt_resolved_pivot1, 10, Scalar(254), 2);
#endif

#if 1
		blob0->fill(image_visualization, 127);
		for (Point& pt : blob1->data)
			image_visualization.ptr<uchar>(pt.y, pt.x +	100)[0] = 254;

		line(image_visualization, blob0->pt_y_max, Point(blob1->pt_y_max.x + 100, blob1->pt_y_max.y), Scalar(127), 1);
#endif
	}

	for (float& confidence : confidence_vec)
		confidence /= confidence_max;

	if (visualize)
		imshow("image_visualizationkladhflkjasdhf", image_visualization);
}