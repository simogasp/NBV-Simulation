#include "NBV_Planner.h"

NBV_Planner::NBV_Planner(Share_Data *_share_data, int _status) {
    share_data = _share_data;
    iterations = 0;
    status = _status;
    share_data->now_view_space_processed = false;
    share_data->now_views_information_processed = false;
    share_data->move_on = false;
    voxel_information = new Voxel_Information(share_data->p_unknown_lower_bound, share_data->p_unknown_upper_bound);
    // Initializing GT
    share_data->access_directory(share_data->save_path);
    // GT cloud
    share_data->cloud_ground_truth->is_dense = false;
    share_data->cloud_ground_truth->points.resize(share_data->cloud_pcd->points.size());
    share_data->cloud_ground_truth->width = share_data->cloud_pcd->points.size();
    share_data->cloud_ground_truth->height = 1;
    auto ptr = share_data->cloud_ground_truth->points.begin();
    auto p = share_data->cloud_pcd->points.begin();
    float unit = 1.0;
    for (auto &ptr: share_data->cloud_pcd->points) {
        if (fabs(ptr.x) >= 10 || fabs(ptr.y) >= 10 || fabs(ptr.z) >= 10) {
            unit = 0.001;
            cout << "change unit from <mm> to <m>." << endl;
            break;
        }
    }
    // Check the size of the object and scale it uniformly to about 0.15m
    vector<Eigen::Vector3d> points;
    for (auto &ptr: share_data->cloud_pcd->points) {
        Eigen::Vector3d pt(ptr.x * unit, ptr.y * unit, ptr.z * unit);
        points.push_back(pt);
    }
    Eigen::Vector3d object_center_world = Eigen::Vector3d(0, 0, 0);
    // Calculating point cloud center of mass
    for (auto &ptr: points) {
        object_center_world(0) += ptr(0);
        object_center_world(1) += ptr(1);
        object_center_world(2) += ptr(2);
    }
    object_center_world(0) /= points.size();
    object_center_world(1) /= points.size();
    object_center_world(2) /= points.size();
    // Dichotomous search of the BBX radius, terminated by the ratio of the number of points in the BBX reaching
    // 0.90 - 0.95
    double l = 0, r = 0, mid;
    for (auto &ptr: points) {
        r = max(r, (object_center_world - ptr).norm());
    }
    mid = (l + r) / 2;
    double precent = check_size(mid, object_center_world, points);
    double pre_precent = precent;
    // @FIXME condition always true, maybe it's a AND condition?
    while (precent > 0.92 || precent < 1.0) {
        if (precent > 0.92) {
            r = mid;
        } else if (precent < 1.0) {
            l = mid;
        }
        mid = (l + r) / 2;
        precent = check_size(mid, object_center_world, points);
        if (fabs(pre_precent - precent) < 0.001)
            break;
        pre_precent = precent;
    }
    double predicted_size = 1.2 * mid;
    float scale = 1.0;
    if (predicted_size > 0.1) {
        scale = 0.1 / predicted_size;
        cout << "object large. change scale to about 0.1 m." << endl;
    }
    // Convert point clouds
    for (int i = 0; i < share_data->cloud_pcd->points.size(); i++, p++) {
        (*ptr).x = (*p).x * scale * unit;
        (*ptr).y = (*p).y * scale * unit;
        (*ptr).z = (*p).z * scale * unit;
        (*ptr).b = 168;
        (*ptr).g = 168;
        (*ptr).r = 168;
        // GT Insertion Point Cloud
        octomap::OcTreeKey key;
        bool key_have =
                share_data->ground_truth_model->coordToKeyChecked(octomap::point3d((*ptr).x, (*ptr).y, (*ptr).z), key);
        if (key_have) {
            octomap::ColorOcTreeNode *voxel = share_data->ground_truth_model->search(key);
            if (voxel == nullptr) {
                share_data->ground_truth_model->setNodeValue(
                        key, share_data->ground_truth_model->getProbHitLog(), true);
                share_data->ground_truth_model->integrateNodeColor(key, (*ptr).r, (*ptr).g, (*ptr).b);
            }
        }
        // GT_sample insert point cloud
        octomap::OcTreeKey key_sp;
        bool key_have_sp =
                share_data->GT_sample->coordToKeyChecked(octomap::point3d((*ptr).x, (*ptr).y, (*ptr).z), key_sp);
        if (key_have_sp) {
            octomap::ColorOcTreeNode *voxel_sp = share_data->GT_sample->search(key_sp);
            if (voxel_sp == nullptr) {
                share_data->GT_sample->setNodeValue(key_sp, share_data->GT_sample->getProbHitLog(), true);
                share_data->GT_sample->integrateNodeColor(key_sp, (*ptr).r, (*ptr).g, (*ptr).b);
            }
        }
        ptr++;
    }
    share_data->ground_truth_model->updateInnerOccupancy();
    share_data->ground_truth_model->write(share_data->save_path + "/GT.ot");
    // GT_sample_voxels
    share_data->GT_sample->updateInnerOccupancy();
    share_data->GT_sample->write(share_data->save_path + "/GT_sample.ot");
    share_data->init_voxels = 0;
    for (octomap::ColorOcTree::leaf_iterator it = share_data->GT_sample->begin_leafs(),
                 end = share_data->GT_sample->end_leafs();
         it != end;
         ++it) {
        share_data->init_voxels++;
    }
    cout << "Map_GT_sample has voxels " << share_data->init_voxels << endl;
    ofstream fout(share_data->save_path + "/GT_sample_voxels.txt");
    fout << share_data->init_voxels << endl;
    // Initialize viewspace
    now_view_space = new View_Space(iterations, share_data, voxel_information, share_data->cloud_ground_truth);
    // Set the initial viewpoint to a uniform position
    now_view_space->views[0].vis++;
    now_best_view = new View(now_view_space->views[0]);
    now_best_view->get_next_camera_pos(share_data->now_camera_pose_world, share_data->object_center_world);
    Eigen::Matrix4d view_pose_world = (share_data->now_camera_pose_world * now_best_view->pose.inverse()).eval();
    // Camera class initialization
    percept = new Perception_3D(share_data);
    if (share_data->show) {
        // Show BBX, camera position, GT
        pcl::visualization::PCLVisualizer::Ptr viewer =
                pcl::visualization::PCLVisualizer::Ptr(new pcl::visualization::PCLVisualizer("Iteration"));
        viewer->setBackgroundColor(0, 0, 0);
        viewer->addCoordinateSystem(0.1);
        viewer->initCameraParameters();
        // First frame camera position
        Eigen::Vector4d X(0.05, 0, 0, 1);
        Eigen::Vector4d Y(0, 0.05, 0, 1);
        Eigen::Vector4d Z(0, 0, 0.05, 1);
        Eigen::Vector4d O(0, 0, 0, 1);
        X = view_pose_world * X;
        Y = view_pose_world * Y;
        Z = view_pose_world * Z;
        O = view_pose_world * O;
        viewer->addLine<pcl::PointXYZ>(
                pcl::PointXYZ(O(0), O(1), O(2)), pcl::PointXYZ(X(0), X(1), X(2)), 255, 0, 0, "X" + to_string(-1));
        viewer->addLine<pcl::PointXYZ>(
                pcl::PointXYZ(O(0), O(1), O(2)), pcl::PointXYZ(Y(0), Y(1), Y(2)), 0, 255, 0, "Y" + to_string(-1));
        viewer->addLine<pcl::PointXYZ>(
                pcl::PointXYZ(O(0), O(1), O(2)), pcl::PointXYZ(Z(0), Z(1), Z(2)), 0, 0, 255, "Z" + to_string(-1));
        // test_viewspace
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr test_viewspace(new pcl::PointCloud<pcl::PointXYZRGB>);
        test_viewspace->is_dense = false;
        test_viewspace->points.resize(now_view_space->views.size());
        auto pt = test_viewspace->points.begin();
        for (int i = 0; i < now_view_space->views.size(); i++, pt++) {
            (*pt).x = now_view_space->views[i].init_pos(0);
            (*pt).y = now_view_space->views[i].init_pos(1);
            (*pt).z = now_view_space->views[i].init_pos(2);
            // First display of all dots in white
            (*pt).r = 255, (*pt).g = 255, (*pt).b = 255;
        }
        viewer->addPointCloud<pcl::PointXYZRGB>(test_viewspace, "test_viewspace");
        now_view_space->add_bbx_to_cloud(viewer);
        viewer->addPointCloud<pcl::PointXYZRGB>(share_data->cloud_ground_truth, "cloud_ground_truth");
        while (!viewer->wasStopped()) {
            viewer->spin();
            boost::this_thread::sleep(boost::posix_time::microseconds(100000));
        }
    }
}

double NBV_Planner::check_size(double predicted_size, Eigen::Vector3d object_center_world,
                               std::vector<Eigen::Vector3d> &points) {
    int vaild_points = 0;
    for (auto &ptr: points) {
        if (ptr(0) < object_center_world(0) - predicted_size || ptr(0) > object_center_world(0) + predicted_size)
            continue;
        if (ptr(1) < object_center_world(1) - predicted_size || ptr(1) > object_center_world(1) + predicted_size)
            continue;
        if (ptr(2) < object_center_world(2) - predicted_size || ptr(2) > object_center_world(2) + predicted_size)
            continue;
        vaild_points++;
    }
    return (double) vaild_points / (double) points.size();
}

int NBV_Planner::plan() {
    switch (status) {
        case Over:
            break;
        case WaitData:
            if (percept->precept(now_best_view)) {
                thread next_view_space(create_view_space, &now_view_space, now_best_view, share_data, iterations);
                next_view_space.detach();
                status = WaitViewSpace;
            }
            break;
        case WaitViewSpace:
            if (share_data->now_view_space_processed) {
                thread next_views_information(create_views_information,
                                              &now_views_information,
                                              now_best_view,
                                              now_view_space,
                                              share_data,
                                              this,
                                              iterations);
                next_views_information.detach();
                status = WaitInformation;
            }
            break;
        case WaitInformation:
            if (share_data->now_views_information_processed) {
                if (share_data->method_of_IG == 6) { // NBV-NET
                    share_data->access_directory(share_data->nbv_net_path + "/log");
                    ifstream ftest;
                    do {
                        ftest.open(share_data->nbv_net_path + "/log/ready.txt");
                    } while (!ftest.is_open());
                    ftest.close();
                    ifstream fin(share_data->nbv_net_path + "/log/" + share_data->name_of_pcd + '_' +
                                 to_string(iterations) + ".txt");
                    double x, y, z, a, b, c;
                    fin >> x >> y >> z >> a >> b >> c;
                    cout << x << " " << y << " " << z << endl;
                    now_best_view->init_pos(0) = x;
                    now_best_view->init_pos(1) = y;
                    now_best_view->init_pos(2) = z;
                    Eigen::Matrix3d rotation;
                    rotation = Eigen::AngleAxisd(a, Eigen::Vector3d::UnitX()) *
                               Eigen::AngleAxisd(b, Eigen::Vector3d::UnitY()) *
                               Eigen::AngleAxisd(c, Eigen::Vector3d::UnitZ());
                    Eigen::Matrix4d R(Eigen::Matrix4d::Identity(4, 4));
                    R(0, 0) = rotation(0, 0);
                    R(0, 1) = rotation(0, 1);
                    R(0, 2) = rotation(0, 2);
                    R(0, 3) = x;
                    R(1, 0) = rotation(1, 0);
                    R(1, 1) = rotation(1, 1);
                    R(1, 2) = rotation(1, 2);
                    R(1, 3) = y;
                    R(2, 0) = rotation(2, 0);
                    R(2, 1) = rotation(2, 1);
                    R(2, 2) = rotation(2, 2);
                    R(2, 3) = z;
                    R(3, 0) = 0;
                    R(3, 1) = 0;
                    R(3, 2) = 0;
                    R(3, 3) = 1;
                    now_best_view->pose = R;
                    this_thread::sleep_for(chrono::seconds(5));
                    int removed = remove((share_data->nbv_net_path + "/log/ready.txt").c_str());
                    if (removed != 0)
                        cout << "cannot remove ready.txt." << endl;
                } else {
                    // Search algorithms
                    // Sorting of viewpoints
                    sort(now_view_space->views.begin(), now_view_space->views.end(), view_utility_compare);
                    /*if (share_data->sum_local_information == 0) {
                            cout << "randomly choose a view" << endl;
                            srand(clock());
                            random_shuffle(now_view_space->views.begin(), now_view_space->views.end());
                    }*/
                    // informed_viewspace
                    if (share_data->show) {
                        // Show BBX with camera position
                        pcl::visualization::PCLVisualizer::Ptr viewer = pcl::visualization::PCLVisualizer::Ptr(
                                new pcl::visualization::PCLVisualizer("Iteration" + to_string(iterations)));
                        viewer->setBackgroundColor(0, 0, 0);
                        viewer->addCoordinateSystem(0.1);
                        viewer->initCameraParameters();
                        // test_viewspace
                        pcl::PointCloud<pcl::PointXYZRGB>::Ptr test_viewspace(
                                new pcl::PointCloud<pcl::PointXYZRGB>);
                        test_viewspace->is_dense = false;
                        test_viewspace->points.resize(now_view_space->views.size());
                        auto ptr = test_viewspace->points.begin();
                        int needed = 0;
                        for (int i = 0; i < now_view_space->views.size(); i++) {
                            (*ptr).x = now_view_space->views[i].init_pos(0);
                            (*ptr).y = now_view_space->views[i].init_pos(1);
                            (*ptr).z = now_view_space->views[i].init_pos(2);
                            // Visited points are recorded in blue
                            if (now_view_space->views[i].vis)
                                (*ptr).r = 0, (*ptr).g = 0, (*ptr).b = 255;
                                // The setting within the network stream is yellow
                            else if (now_view_space->views[i].in_coverage[iterations] &&
                                     i < now_view_space->views.size() / 10)
                                (*ptr).r = 255, (*ptr).g = 255, (*ptr).b = 0;
                                // The setting within the network stream is green
                            else if (now_view_space->views[i].in_coverage[iterations])
                                (*ptr).r = 255, (*ptr).g = 0, (*ptr).b = 0;
                                // The top 10% of the weighted points are set to blue-green
                            else if (i < now_view_space->views.size() / 10)
                                (*ptr).r = 0, (*ptr).g = 255, (*ptr).b = 255;
                                // Don't want the rest of the points
                            else
                                continue;
                            ptr++;
                            needed++;
                        }
                        test_viewspace->points.resize(needed);
                        viewer->addPointCloud<pcl::PointXYZRGB>(test_viewspace, "test_viewspace");
                        bool best_have = false;
                        for (int i = 0; i < now_view_space->views.size(); i++) {
                            if (now_view_space->views[i].vis) {
                                now_view_space->views[i].get_next_camera_pos(share_data->now_camera_pose_world,
                                                                             share_data->object_center_world);
                                Eigen::Matrix4d view_pose_world =
                                        (share_data->now_camera_pose_world * now_view_space->views[i].pose.inverse())
                                                .eval();
                                Eigen::Vector4d X(0.03, 0, 0, 1);
                                Eigen::Vector4d Y(0, 0.03, 0, 1);
                                Eigen::Vector4d Z(0, 0, 0.03, 1);
                                Eigen::Vector4d O(0, 0, 0, 1);
                                X = view_pose_world * X;
                                Y = view_pose_world * Y;
                                Z = view_pose_world * Z;
                                O = view_pose_world * O;
                                viewer->addLine<pcl::PointXYZ>(pcl::PointXYZ(O(0), O(1), O(2)),
                                                               pcl::PointXYZ(X(0), X(1), X(2)),
                                                               255,
                                                               0,
                                                               0,
                                                               "X" + to_string(i));
                                viewer->addLine<pcl::PointXYZ>(pcl::PointXYZ(O(0), O(1), O(2)),
                                                               pcl::PointXYZ(Y(0), Y(1), Y(2)),
                                                               0,
                                                               255,
                                                               0,
                                                               "Y" + to_string(i));
                                viewer->addLine<pcl::PointXYZ>(pcl::PointXYZ(O(0), O(1), O(2)),
                                                               pcl::PointXYZ(Z(0), Z(1), Z(2)),
                                                               0,
                                                               0,
                                                               255,
                                                               "Z" + to_string(i));
                            } else if (!best_have) {
                                now_view_space->views[i].get_next_camera_pos(share_data->now_camera_pose_world,
                                                                             share_data->object_center_world);
                                Eigen::Matrix4d view_pose_world =
                                        (share_data->now_camera_pose_world * now_view_space->views[i].pose.inverse())
                                                .eval();
                                Eigen::Vector4d X(0.08, 0, 0, 1);
                                Eigen::Vector4d Y(0, 0.08, 0, 1);
                                Eigen::Vector4d Z(0, 0, 0.08, 1);
                                Eigen::Vector4d O(0, 0, 0, 1);
                                X = view_pose_world * X;
                                Y = view_pose_world * Y;
                                Z = view_pose_world * Z;
                                O = view_pose_world * O;
                                viewer->addLine<pcl::PointXYZ>(pcl::PointXYZ(O(0), O(1), O(2)),
                                                               pcl::PointXYZ(X(0), X(1), X(2)),
                                                               255,
                                                               0,
                                                               0,
                                                               "X" + to_string(i));
                                viewer->addLine<pcl::PointXYZ>(pcl::PointXYZ(O(0), O(1), O(2)),
                                                               pcl::PointXYZ(Y(0), Y(1), Y(2)),
                                                               0,
                                                               255,
                                                               0,
                                                               "Y" + to_string(i));
                                viewer->addLine<pcl::PointXYZ>(pcl::PointXYZ(O(0), O(1), O(2)),
                                                               pcl::PointXYZ(Z(0), Z(1), Z(2)),
                                                               0,
                                                               0,
                                                               255,
                                                               "Z" + to_string(i));
                                best_have = true;
                            }
                        }
                        viewer->addPointCloud<pcl::PointXYZRGB>(share_data->cloud_final, "cloud_now_itreation");
                        while (!viewer->wasStopped()) {
                            viewer->spin();
                            boost::this_thread::sleep(boost::posix_time::microseconds(100000));
                        }
                    }
                    double max_utility = -1;
                    for (int i = 0; i < now_view_space->views.size(); i++) {
                        cout << "checking view " << i << endl;
                        if (now_view_space->views[i].vis)
                            continue;
                        // if (!now_view_space->views[i].can_move) continue;
                        now_best_view = new View(now_view_space->views[i]);
                        max_utility = now_best_view->final_utility;
                        now_view_space->views[i].vis++;
                        now_view_space->views[i].can_move = true;
                        cout << "choose the " << i << "th view." << endl;
                        break;
                    }
                    if (max_utility == -1) {
                        cout << "Can't move to any viewport.Stop." << endl;
                        status = Over;
                        break;
                    }
                    cout << " next best view pos is (" << now_best_view->init_pos(0) << ", "
                         << now_best_view->init_pos(1) << ", " << now_best_view->init_pos(2) << ")" << endl;
                    cout << " next best view final_utility is " << now_best_view->final_utility << endl;
                }
                thread next_moving(move_robot, now_best_view, now_view_space, share_data, this);
                next_moving.detach();
                status = WaitMoving;
            }
            break;
        case WaitMoving:
            // virtual move
            if (share_data->over) {
                cout << "Progress over.Saving octomap and cloud." << endl;
                status = Over;
                break;
            }
            if (share_data->move_on) {
                iterations++;
                share_data->now_view_space_processed = false;
                share_data->now_views_information_processed = false;
                share_data->move_on = false;
                status = WaitData;
            }
            break;
    }
    return status;
}

string NBV_Planner::out_status() {
    string status_string;
    switch (status) {
        case Over:
            status_string = "Over";
            break;
        case WaitData:
            status_string = "WaitData";
            break;
        case WaitViewSpace:
            status_string = "WaitViewSpace";
            break;
        case WaitInformation:
            status_string = "WaitInformation";
            break;
        case WaitMoving:
            status_string = "WaitMoving";
            break;
    }
    return status_string;
}

void save_cloud_mid_thread_process(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, string name, Share_Data *share_data) {
    // Thread for saving intermediate point clouds, currently not checking if the save is complete
    share_data->save_cloud_to_disk(cloud, "/clouds", name);
    cout << name << " saved" << endl;
}

void create_view_space(View_Space **now_view_space, View *now_best_view, Share_Data *share_data, int iterations) {
    // Calculating keyframe camera poses
    share_data->now_camera_pose_world = (share_data->now_camera_pose_world * now_best_view->pose.inverse()).eval();;
    // Handling viewspace
    (*now_view_space)->update(iterations, share_data, share_data->cloud_final, share_data->clouds[iterations]);
    // Save the results of intermediate iterations
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_mid(new pcl::PointCloud<pcl::PointXYZRGB>);
    *cloud_mid = *share_data->cloud_final;
    thread save_mid(save_cloud_mid_thread_process, cloud_mid, "pointcloud" + to_string(iterations), share_data);
    save_mid.detach();
    // Update flag bit
    share_data->now_view_space_processed = true;
}

void create_views_information(Views_Information **now_views_information,
                              View *now_best_view,
                              View_Space *now_view_space,
                              Share_Data *share_data,
                              NBV_Planner *nbv_plan,
                              int iterations) {
    if (share_data->method_of_IG == 6) { // NBV-NET
        // scale
        share_data->access_directory(share_data->nbv_net_path + "/viewspace");
        ofstream fout_vs(share_data->nbv_net_path + "/viewspace/" + share_data->name_of_pcd + ".txt");
        double scale_of_object = 0;
        double x1 = now_view_space->object_center_world(0) - now_view_space->predicted_size;
        double x2 = now_view_space->object_center_world(0) + now_view_space->predicted_size;
        double y1 = now_view_space->object_center_world(1) - now_view_space->predicted_size;
        double y2 = now_view_space->object_center_world(1) + now_view_space->predicted_size;
        double z1 = now_view_space->object_center_world(2) - now_view_space->predicted_size;
        double z2 = now_view_space->object_center_world(2) + now_view_space->predicted_size;
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x1, y1, z1).eval()).norm());
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x1, y2, z1).eval()).norm());
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x1, y1, z2).eval()).norm());
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x1, y2, z2).eval()).norm());
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x2, y1, z1).eval()).norm());
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x2, y2, z1).eval()).norm());
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x2, y1, z2).eval()).norm());
        scale_of_object = max(scale_of_object, (Eigen::Vector3d(x2, y2, z2).eval()).norm());
        fout_vs << scale_of_object * 2.0 << '\n';
        // octotree
        share_data->access_directory(share_data->nbv_net_path + "/data");
        ofstream fout(share_data->nbv_net_path + "/data/" + share_data->name_of_pcd + '_' + to_string(iterations) +
                      ".txt");
        for (octomap::ColorOcTree::leaf_iterator it = share_data->octo_model->begin_leafs(),
                     end = share_data->octo_model->end_leafs();
             it != end;
             ++it) {
            double x = it.getX();
            double y = it.getY();
            double z = it.getZ();
            if (x >= now_view_space->object_center_world(0) - now_view_space->predicted_size &&
                x <= now_view_space->object_center_world(0) + now_view_space->predicted_size &&
                y >= now_view_space->object_center_world(1) - now_view_space->predicted_size &&
                y <= now_view_space->object_center_world(1) + now_view_space->predicted_size &&
                z >= now_view_space->object_center_world(2) - now_view_space->predicted_size &&
                z <= now_view_space->object_center_world(2) + now_view_space->predicted_size) {
                double occupancy = (*it).getOccupancy();
                fout << x << ' ' << y << ' ' << z << ' ' << occupancy << '\n';
            }
        }
    } else { // Search by
        // Processing views_information
        if (iterations == 0)
            (*now_views_information) =
                    new Views_Information(share_data, nbv_plan->voxel_information, now_view_space, iterations);
        else
            (*now_views_information)->update(share_data, now_view_space, iterations);
        if (share_data->method_of_IG == OursIG) {
            // Handling network streams and obtaining global optimisation functions
            views_voxels_MF *set_cover_solver = new views_voxels_MF(share_data->num_of_max_flow_node,
                                                                    now_view_space,
                                                                    *now_views_information,
                                                                    nbv_plan->voxel_information,
                                                                    share_data);
            set_cover_solver->solve();
            vector<int> coverage_view_id_set = set_cover_solver->get_view_id_set();
            for (int i = 0; i < coverage_view_id_set.size(); i++)
                now_view_space->views[coverage_view_id_set[i]].in_coverage[iterations] = 1;
        }
        // Combined calculation of local greed and global optimization to produce viewpoint information entropy
        share_data->sum_local_information = 0;
        share_data->sum_global_information = 0;
        share_data->sum_robot_cost = 0;
        for (int i = 0; i < now_view_space->views.size(); i++) {
            share_data->sum_local_information += now_view_space->views[i].information_gain;
            share_data->sum_global_information += now_view_space->views[i].get_global_information();
            share_data->sum_robot_cost += now_view_space->views[i].robot_cost;
        }
        if (share_data->sum_local_information == 0)
            cout << "full information is zero." << endl;
        for (int i = 0; i < now_view_space->views.size(); i++) {
            if (share_data->method_of_IG == OursIG)
                now_view_space->views[i].final_utility =
                        (1 - share_data->cost_weight) * now_view_space->views[i].information_gain /
                        share_data->sum_local_information +
                        share_data->cost_weight * now_view_space->views[i].get_global_information() /
                        share_data->sum_global_information;
            else if (share_data->method_of_IG == APORA)
                now_view_space->views[i].final_utility = now_view_space->views[i].information_gain;
            else
                now_view_space->views[i].final_utility =
                        0.7 * (share_data->sum_local_information == 0
                               ? 0
                               : now_view_space->views[i].information_gain / share_data->sum_local_information) +
                                0.3 * (share_data->robot_cost_negative == true ? -1 : 1) * now_view_space->views[i].robot_cost /
                                share_data->sum_robot_cost;
        }
    }
    // Update flag bit
    share_data->now_views_information_processed = true;
}

void move_robot(View *now_best_view, View_Space *now_view_space, Share_Data *share_data, NBV_Planner *nbv_plan) {
    if (share_data->num_of_max_iteration > 0 && nbv_plan->iterations + 1 >= share_data->num_of_max_iteration)
        share_data->over = true;
    if (!share_data->move_wait)
        share_data->move_on = true;
}

[[maybe_unused]] void show_cloud(pcl::visualization::PCLVisualizer::Ptr viewer) {
    // pcl display point cloud
    while (!viewer->wasStopped()) {
        viewer->spin();
        boost::this_thread::sleep(boost::posix_time::microseconds(100000));
    }
}