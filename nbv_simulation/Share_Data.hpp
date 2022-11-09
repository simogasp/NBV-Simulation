#pragma once
#include <octomap/ColorOcTree.h>
#include <octomap/octomap.h>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/xfeatures2d.hpp>
#include <pcl/common/io.h>
#include <pcl/filters/passthrough.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/visualization/pcl_visualizer.h>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <algorithm>
#include <atomic>
#include <chrono>
//#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "utils.cpp"

namespace fs = std::filesystem;

using namespace std;

#define OursIG 0
#define OA 1
#define UV 2
#define RSE 3
#define APORA 4
#define Kr 5
#define NBVNET 6
#define PCNBV 7
#define NewOurs 8

/**
 * The class that contains all the data needed for the NBV algorithm.
 */
class Share_Data
{
  public:
    // Variable input parameters
    string pcd_file_path;
    string yaml_file_path;
    string name_of_pcd;
    string nbv_net_path;

    // Number of viewpoints sampled at one time
    int num_of_views;
    double cost_weight;
    rs2_intrinsics color_intrinsics;
    double depth_scale;

    // Operating parameters
    // Process number
    int process_cnt;
    // System clock
    atomic<double> pre_clock;
    // Is the process finished
    atomic<bool> over;
    bool show;
    int num_of_max_iteration;

    // Point cloud data
    atomic<int> vaild_clouds;
    // Point cloud group
    vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> clouds;
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_pcd;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_ground_truth;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_final;
    bool move_wait;

    // Eight Forks Map
    octomap::ColorOcTree* octo_model;
    // octomap::ColorOcTree* cloud_model;
    octomap::ColorOcTree* ground_truth_model;
    octomap::ColorOcTree* GT_sample;
    double octomap_resolution;
    double ground_truth_resolution;
    double map_size;
    double p_unknown_upper_bound; //! Upper bound for voxels to still be considered uncertain. Default: 0.97.
    double p_unknown_lower_bound; //! Lower bound for voxels to still be considered uncertain. Default: 0.12.

    // Workspace and viewpoint space
    atomic<bool> now_view_space_processed;
    atomic<bool> now_views_infromation_processed;
    atomic<bool> move_on;

    Eigen::Matrix4d now_camera_pose_world;
    // Object centre
    Eigen::Vector3d object_center_world;
    // Object BBX radius
    double predicted_size;

    int method_of_IG;
    pcl::visualization::PCLVisualizer::Ptr viewer;

    double stop_thresh_map;
    double stop_thresh_view;

    double skip_coefficient;

    double sum_local_information;
    double sum_global_information;

    double sum_robot_cost;
    double camera_to_object_dis;
    bool robot_cost_negtive;

    int num_of_max_flow_node;
    double interesting_threshold;

    // Number of point cloud voxels
    int init_voxels;
    // Number of voxels on the map
    int voxels_in_BBX;
    // Map information entropy
    double init_entropy;

    string save_path;

    Share_Data(string _config_file_path)
    {
        process_cnt = -1;
        yaml_file_path = _config_file_path;
        // Reading yaml files
        cv::FileStorage fs;
        fs.open(yaml_file_path, cv::FileStorage::READ);
        fs["model_path"] >> pcd_file_path;
        fs["name_of_pcd"] >> name_of_pcd;
        fs["method_of_IG"] >> method_of_IG;
        fs["octomap_resolution"] >> octomap_resolution;
        fs["ground_truth_resolution"] >> ground_truth_resolution;
        fs["num_of_max_iteration"] >> num_of_max_iteration;
        fs["show"] >> show;
        fs["move_wait"] >> move_wait;
        fs["nbv_net_path"] >> nbv_net_path;
        fs["p_unknown_upper_bound"] >> p_unknown_upper_bound;
        fs["p_unknown_lower_bound"] >> p_unknown_lower_bound;
        fs["num_of_views"] >> num_of_views;
        fs["cost_weight"] >> cost_weight;
        fs["robot_cost_negtive"] >> robot_cost_negtive;
        fs["skip_coefficient"] >> skip_coefficient;
        fs["num_of_max_flow_node"] >> num_of_max_flow_node;
        fs["interesting_threshold"] >> interesting_threshold;
        fs["color_width"] >> color_intrinsics.width;
        fs["color_height"] >> color_intrinsics.height;
        fs["color_fx"] >> color_intrinsics.fx;
        fs["color_fy"] >> color_intrinsics.fy;
        fs["color_ppx"] >> color_intrinsics.ppx;
        fs["color_ppy"] >> color_intrinsics.ppy;
        fs["color_model"] >> color_intrinsics.model;
        fs["color_k1"] >> color_intrinsics.coeffs[0];
        fs["color_k2"] >> color_intrinsics.coeffs[1];
        fs["color_k3"] >> color_intrinsics.coeffs[2];
        fs["color_p1"] >> color_intrinsics.coeffs[3];
        fs["color_p2"] >> color_intrinsics.coeffs[4];
        fs["depth_scale"] >> depth_scale;
        fs.release();
        // Read the pcd file of the converted model
        pcl::PointCloud<pcl::PointXYZ>::Ptr temp_pcd(new pcl::PointCloud<pcl::PointXYZ>);
        cloud_pcd = temp_pcd;
        if(pcl::io::loadPCDFile<pcl::PointXYZ>(pcd_file_path + name_of_pcd + ".pcd", *cloud_pcd) == -1)
        {
            cout << "Can not read 3d model file. Check." << endl;
        }
        octo_model = new octomap::ColorOcTree(octomap_resolution);
        // octo_model->setProbHit(0.95);	//Set sensor hit rate, initially 0.7
        // octo_model->setProbMiss(0.05);	//Set sensor error rate, initially 0.4
        // octo_model->setClampingThresMax(1.0);	//Set the maximum value of the map node, initially 0.971
        // octo_model->setClampingThresMin(0.0);	//Set the map node minimum value, initially 0.1192
        // octo_model->setOccupancyThres(0.5);	//Set the node occupancy threshold, initially 0.5
        ground_truth_model = new octomap::ColorOcTree(ground_truth_resolution);
        // ground_truth_model->setProbHit(0.95);	//Set sensor hit rate, initially 0.7
        // ground_truth_model->setProbMiss(0.05);	//Set sensor error rate, initially 0.4
        // ground_truth_model->setClampingThresMax(1.0);	//Set the maximum value of the map node, initially 0.971
        // ground_truth_model->setClampingThresMin(0.0);	//Set the map node minimum value, initially 0.1192
        GT_sample = new octomap::ColorOcTree(octomap_resolution);
        // GT_sample->setProbHit(0.95);	//Set sensor hit rate, initially 0.7
        // GT_sample->setProbMiss(0.05);	//Set sensor error rate, initially 0.4
        // GT_sample->setClampingThresMax(1.0);	//Set the maximum value of the map node, initially 0.971
        // GT_sample->setClampingThresMin(0.0);	//Set the map node minimum value, initially 0.1192
        /*cloud_model = new octomap::ColorOcTree(ground_truth_resolution);
        //cloud_model->setProbHit(0.95);	//Set sensor hit rate, initially 0.7
        //cloud_model->setProbMiss(0.05);	//Set sensor error rate, initially 0.4
        //cloud_model->setClampingThresMax(1.0);	//Set the maximum value of the map node, initially 0.971
        //cloud_model->setClampingThresMin(0.0);	//Set the map node minimum value, initially 0.11921
        //cloud_model->setOccupancyThres(0.5);	//Set the node occupancy threshold, initially 0.5*/
        if(num_of_max_flow_node == -1)
            num_of_max_flow_node = num_of_views;
        now_camera_pose_world = Eigen::Matrix4d::Identity(4, 4);
        over = false;
        pre_clock = clock();
        vaild_clouds = 0;
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr temp(new pcl::PointCloud<pcl::PointXYZRGB>);
        cloud_final = temp;
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr temp_gt(new pcl::PointCloud<pcl::PointXYZRGB>);
        cloud_ground_truth = temp_gt;
        save_path = "../" + name_of_pcd + '_' + to_string(method_of_IG);
        if(method_of_IG == 0)
            save_path += '_' + to_string(cost_weight);
        cout << "pcd and yaml files read." << endl;
        cout << "save_path is: " << save_path << endl;
        srand(clock());
    }

    /**
     * Destructor of the object Share_Data
     */
    ~Share_Data() {}

    /**
     * Return the time used and update the clock
     *
     * @return elapsed_time : the time elapsed since the latest update
     */
    double out_clock()
    {
        double now_clock = clock();
        double elapsed_time = now_clock - pre_clock;
        pre_clock = now_clock;
        return elapsed_time;
    }

    /**
     * Check for the existence of folders in multi-level directories and create them if they do not exist
     * TODO : Change this function to simplify it
     *
     * @param cd The folder path
     */
    void access_directory(string cd)
    {
        string temp;
        for(int i = 0; i < cd.length(); i++)
            if(cd[i] == '/')
            {
                if(access(temp.c_str(), 0) != 0)
                    fs::create_directory(temp);
                temp += cd[i];
            }
            else
                temp += cd[i];
        if(access(temp.c_str(), 0) != 0)
            fs::create_directory(temp);
    }

    /**
     * Storage of rotation matrix data to disk
     * TODO : Function never used
     *
     * @param T The rotation matrix to save
     * @param cd The path to save the file
     * @param name The name of the file
     * @param frames_cnt The frame number
     */
    void save_posetrans_to_disk(Eigen::Matrix4d& T, string cd, string name, int frames_cnt)
    {
        std::stringstream pose_stream, path_stream;
        std::string pose_file, path;
        path_stream << "../data"
                    << "_" << process_cnt << cd;
        path_stream >> path;
        access_directory(path);
        pose_stream << "../data"
                    << "_" << process_cnt << cd << "/" << name << "_" << frames_cnt << ".txt";
        pose_stream >> pose_file;
        ofstream fout(pose_file);
        fout << T;
    }

    /**
     * Save the octomap logs to the disk
     * TODO : Function never used
     *
     * @param voxels
     * @param entropy
     * @param cd
     * @param name
     * @param iterations
     */
    void save_octomap_log_to_disk(int voxels, double entropy, string cd, string name, int iterations)
    {
        std::stringstream log_stream, path_stream;
        std::string log_file, path;
        path_stream << "../data"
                    << "_" << process_cnt << cd;
        path_stream >> path;
        access_directory(path);
        log_stream << "../data"
                   << "_" << process_cnt << cd << "/" << name << "_" << iterations << ".txt";
        log_stream >> log_file;
        ofstream fout(log_file);
        fout << voxels << " " << entropy << endl;
    }

    /**
     * Store point cloud data to disk, very slow, rarely used
     *
     * @param cloud The point cloud to save
     * @param cd The path to save the point cloud
     * @param name The name of the point cloud to save
     */
    void save_cloud_to_disk(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, string cd, string name)
    {
        std::stringstream cloud_stream, path_stream;
        std::string cloud_file, path;
        path_stream << save_path << cd;
        path_stream >> path;
        access_directory(path);
        cloud_stream << save_path << cd << "/" << name << ".pcd";
        cloud_stream >> cloud_file;
        pcl::io::savePCDFile<pcl::PointXYZRGB>(cloud_file, *cloud);
    }

    /**
     * Store point cloud data to disk, very slow, rarely used
     * TODO : Function never used
     *
     * @param cloud The point cloud to save
     * @param cd The path to save the point cloud
     * @param name The name of the point cloud to save
     * @param frames_cnt The number of frames
     */
    void save_cloud_to_disk(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, string cd, string name, int frames_cnt)
    {
        std::stringstream cloud_stream, path_stream;
        std::string cloud_file, path;
        path_stream << "../data"
                    << "_" << process_cnt << cd;
        path_stream >> path;
        access_directory(path);
        cloud_stream << "../data"
                     << "_" << process_cnt << cd << "/" << name << "_" << frames_cnt << ".pcd";
        cloud_stream >> cloud_file;
        pcl::io::savePCDFile<pcl::PointXYZRGB>(cloud_file, *cloud);
    }

    /**
     * Store point cloud data to disk, very slow, rarely used
     * TODO : Function never used
     *
     * @param octo_model The octree to save
     * @param cd The path to save the octree
     * @param name The name of the octree to save
     */
    void save_octomap_to_disk(octomap::ColorOcTree* octo_model, string cd, string name)
    {

        std::stringstream octomap_stream, path_stream;
        std::string octomap_file, path;
        path_stream << "../data"
                    << "_" << process_cnt << cd;
        path_stream >> path;
        access_directory(path);
        octomap_stream << "../data"
                       << "_" << process_cnt << cd << "/" << name << ".ot";
        octomap_stream >> octomap_file;
        octo_model->write(octomap_file);
    }
};

/**
 * Compute the square of a number
 * @param x The number to compute the square
 * @return The square of the number
 */
inline double pow2(double x) { return x * x; }