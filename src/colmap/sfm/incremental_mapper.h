// Copyright (c), ETH Zurich and UNC Chapel Hill.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of ETH Zurich and UNC Chapel Hill nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "colmap/estimators/bundle_adjustment.h"
#include "colmap/scene/database.h"
#include "colmap/scene/database_cache.h"
#include "colmap/scene/reconstruction.h"
#include "colmap/sfm/incremental_triangulator.h"
#include "colmap/sfm/observation_manager.h"

namespace colmap {

// Class that provides all functionality for the incremental reconstruction
// procedure. Example usage:
//
//  IncrementalMapper mapper(&database_cache);
//  mapper.BeginReconstruction(&reconstruction);
//  TwoViewGeometry tvg;
//  THROW_CHECK(
//      mapper.FindInitialImagePair(options, tvg, image_id1, image_id2));
//  mapper.RegisterInitialImagePair(options, tvg, image_id1, image_id2);
//  while (...) {
//    const auto next_image_ids = mapper.FindNextImages(options);
//    for (const auto image_id : next_image_ids) {
//      THROW_CHECK(mapper.RegisterNextImage(options, image_id));
//      if (...) {
//        mapper.AdjustLocalBundle(...);
//      } else {
//        mapper.AdjustGlobalBundle(...);
//      }
//    }
//  }
//  mapper.EndReconstruction(false);
//
class IncrementalMapper {
 public:
  struct Options {
    // Minimum number of inliers for initial image pair.
    int init_min_num_inliers = 100;

    // Maximum error in pixels for two-view geometry estimation for initial
    // image pair.
    double init_max_error = 4.0;

    // Maximum forward motion for initial image pair.
    double init_max_forward_motion = 0.95;

    // Minimum triangulation angle for initial image pair.
    double init_min_tri_angle = 16.0;

    // Maximum number of trials to use an image for initialization.
    int init_max_reg_trials = 2;

    // Maximum reprojection error in absolute pose estimation.
    double abs_pose_max_error = 12.0;

    // Minimum number of inliers in absolute pose estimation.
    int abs_pose_min_num_inliers = 30;

    // Minimum inlier ratio in absolute pose estimation.
    double abs_pose_min_inlier_ratio = 0.25;

    // Whether to estimate the focal length in absolute pose estimation.
    bool abs_pose_refine_focal_length = true;

    // Whether to estimate the extra parameters in absolute pose estimation.
    bool abs_pose_refine_extra_params = true;

    // Number of images to optimize in local bundle adjustment.
    int local_ba_num_images = 6;

    // Minimum triangulation for images to be chosen in local bundle adjustment.
    double local_ba_min_tri_angle = 6;

    // Thresholds for bogus camera parameters. Images with bogus camera
    // parameters are filtered and ignored in triangulation.
    double min_focal_length_ratio = 0.1;  // Opening angle of ~130deg
    double max_focal_length_ratio = 10;   // Opening angle of ~5deg
    double max_extra_param = 1;

    // Maximum reprojection error in pixels for observations.
    double filter_max_reproj_error = 4.0;

    // Minimum triangulation angle in degrees for stable 3D points.
    double filter_min_tri_angle = 1.5;

    // Maximum number of trials to register an image.
    int max_reg_trials = 3;

    // If reconstruction is provided as input, fix the existing image poses.
    bool fix_existing_frames = false;

    // Whether to use prior camera positions
    bool use_prior_position = false;

    // Whether to use a robust loss on prior locations
    bool use_robust_loss_on_prior_position = false;

    // Threshold on the residual for the robust loss
    // (chi2 for 3DOF at 95% = 7.815)
    double prior_position_loss_scale = 7.815;

    // Number of threads.
    int num_threads = -1;

    // Method to find and select next best image to register.
    enum class ImageSelectionMethod {
      MAX_VISIBLE_POINTS_NUM,
      MAX_VISIBLE_POINTS_RATIO,
      MIN_UNCERTAINTY,
    };
    ImageSelectionMethod image_selection_method =
        ImageSelectionMethod::MIN_UNCERTAINTY;

    bool Check() const;
  };

  struct LocalBundleAdjustmentReport {
    size_t num_merged_observations = 0;
    size_t num_completed_observations = 0;
    size_t num_filtered_observations = 0;
    size_t num_adjusted_observations = 0;
  };

  // Create incremental mapper. The database cache must live for the entire
  // life-time of the incremental mapper.
  explicit IncrementalMapper(
      std::shared_ptr<const DatabaseCache> database_cache);

  // Prepare the mapper for a new reconstruction, which might have existing
  // registered images (in which case `RegisterNextImage` must be called) or
  // which is empty (in which case `RegisterInitialImagePair` must be called).
  void BeginReconstruction(
      const std::shared_ptr<Reconstruction>& reconstruction);

  // Cleanup the mapper after the current reconstruction is done. If the
  // model is discarded, the number of total and shared registered images will
  // be updated accordingly.
  void EndReconstruction(bool discard);

  // Find initial image pair to seed the incremental reconstruction. The image
  // pairs should be passed to `RegisterInitialImagePair`. This function
  // automatically ignores image pairs that failed to register previously.
  bool FindInitialImagePair(const Options& options,
                            image_t& image_id1,
                            image_t& image_id2,
                            Rigid3d& cam2_from_cam1);

  // Find best next image to register in the incremental reconstruction. The
  // images should be passed to `RegisterNextImage`. This function automatically
  // ignores images that failed to registered for `max_reg_trials`.
  std::vector<image_t> FindNextImages(const Options& options);

  // Attempt to seed the reconstruction from an image pair.
  void RegisterInitialImagePair(const Options& options,
                                image_t image_id1,
                                image_t image_id2,
                                const Rigid3d& cam2_from_cam1);

  // Attempt to register image to the existing model. This requires that
  // a previous call to `RegisterInitialImagePair` was successful.
  bool RegisterNextImage(const Options& options, image_t image_id);

  // Triangulate observations of image.
  size_t TriangulateImage(const IncrementalTriangulator::Options& tri_options,
                          image_t image_id);

  // Retriangulate image pairs that should have common observations according to
  // the scene graph but don't due to drift, etc. To handle drift, the employed
  // reprojection error thresholds should be relatively large. If the thresholds
  // are too large, non-robust bundle adjustment will break down; if the
  // thresholds are too small, we cannot fix drift effectively.
  size_t Retriangulate(const IncrementalTriangulator::Options& tri_options);

  // Complete tracks by transitively following the scene graph correspondences.
  // This is especially effective after bundle adjustment, since many cameras
  // and point locations might have improved. Completion of tracks enables
  // better subsequent registration of new images.
  size_t CompleteTracks(const IncrementalTriangulator::Options& tri_options);

  // Merge tracks by using scene graph correspondences. Similar to
  // `CompleteTracks`, this is effective after bundle adjustment and improves
  // the redundancy in subsequent bundle adjustments.
  size_t MergeTracks(const IncrementalTriangulator::Options& tri_options);

  // Globally complete and merge tracks.
  size_t CompleteAndMergeTracks(
      const IncrementalTriangulator::Options& tri_options);

  // Adjust locally connected images and points of a reference image. In
  // addition, refine the provided 3D points. Only images connected to the
  // reference image are optimized. If the provided 3D points are not locally
  // connected to the reference image, their observing images are set as
  // constant in the adjustment.
  LocalBundleAdjustmentReport AdjustLocalBundle(
      const Options& options,
      const BundleAdjustmentOptions& ba_options,
      const IncrementalTriangulator::Options& tri_options,
      image_t image_id,
      const std::unordered_set<point3D_t>& point3D_ids);

  // Global bundle adjustment using Ceres Solver.
  bool AdjustGlobalBundle(const Options& options,
                          const BundleAdjustmentOptions& ba_options);

  // Perform multiple rounds of local bundle adjustment.
  void IterativeLocalRefinement(
      int max_num_refinements,
      double max_refinement_change,
      const Options& options,
      const BundleAdjustmentOptions& ba_options,
      const IncrementalTriangulator::Options& tri_options,
      image_t image_id);

  // Perform multiple rounds of global bundle adjustment.
  void IterativeGlobalRefinement(
      int max_num_refinements,
      double max_refinement_change,
      const Options& options,
      const BundleAdjustmentOptions& ba_options,
      const IncrementalTriangulator::Options& tri_options,
      bool normalize_reconstruction = true);

  // Filter frames and point observations.
  size_t FilterFrames(const Options& options);
  size_t FilterPoints(const Options& options);

  // Getter functions
  std::shared_ptr<class Reconstruction> Reconstruction() const;
  class ObservationManager& ObservationManager() const;
  IncrementalTriangulator& Triangulator() const;
  const std::unordered_set<frame_t>& FilteredFrames() const;
  const std::unordered_set<frame_t>& ExistingFrameIds() const;
  const std::unordered_map<rig_t, size_t>& NumRegFramesPerRig() const;
  const std::unordered_map<camera_t, size_t>& NumRegImagesPerCamera() const;

  // Reset registration statistics for initialization. This can be used when
  // relaxing the initialization thresholds, such that previously tried pairs
  // will be tried again.
  void ResetInitializationStats();

  // Number of images that are registered in at least on reconstruction.
  size_t NumTotalRegImages() const;

  // Number of shared images between current reconstruction and all other
  // previous reconstructions.
  size_t NumSharedRegImages() const;

  // Get changed 3D points, since the last call to `ClearModifiedPoints3D`.
  const std::unordered_set<point3D_t>& GetModifiedPoints3D();

  // Clear the collection of changed 3D points.
  void ClearModifiedPoints3D();

  // Estimate two view geometry and checks if it is suitable for initialization.
  bool EstimateInitialTwoViewGeometry(const Options& options,
                                      image_t image_id1,
                                      image_t image_id2,
                                      Rigid3d& cam2_from_cam1);

  // Find local bundle for given image in the reconstruction. The local bundle
  // is defined as the images that are most connected, i.e. maximum number of
  // shared 3D points, to the given image.
  std::vector<image_t> FindLocalBundle(const Options& options,
                                       image_t image_id) const;

 private:
  struct RegistrationStatistics {
    // Number of images that are registered in at least one reconstruction.
    size_t num_total_reg_images = 0;

    // Number of shared images between current reconstruction and all other
    // previous reconstructions.
    size_t num_shared_reg_images = 0;

    // Images and image pairs that have been used for initialization. Each image
    // and image pair is only tried once for initialization.
    std::unordered_map<image_t, size_t> init_num_reg_trials;
    std::unordered_set<image_pair_t> init_image_pairs;

    // The number of registered frames/images per rig/camera. This information
    // is used to avoid duplicate refinement of rig/camera parameters and
    // degradation of already refined rig/camera parameters in local bundle
    // adjustment when multiple frames share rigs or images share intrinsics.
    std::unordered_map<rig_t, size_t> num_reg_frames_per_rig;
    std::unordered_map<camera_t, size_t> num_reg_images_per_camera;

    // The number of reconstructions in which images are registered.
    std::unordered_map<image_t, size_t> num_registrations;

    // Number of trials to register image in current reconstruction. Used to set
    // an upper bound to the number of trials to register an image.
    std::unordered_map<image_t, size_t> num_reg_trials;
  };

  // Registers a frame using generalized absolute pose estimation.
  bool RegisterNextGeneralFrame(const Options& options, Frame& frame);

  // Register / De-register frame in current reconstruction and update
  // the (shared) registration statistics.
  void RegisterFrameEvent(frame_t frame_id);
  void DeRegisterFrameEvent(frame_t frame_id);

  // Class that holds all necessary data from database in memory.
  const std::shared_ptr<const DatabaseCache> database_cache_;

  // Class that holds data of the reconstruction.
  std::shared_ptr<class Reconstruction> reconstruction_;

  // Class that is responsible for keeping track of 3D point statistics.
  std::shared_ptr<class ObservationManager> obs_manager_;

  // Class that is responsible for incremental triangulation.
  std::shared_ptr<IncrementalTriangulator> triangulator_;

  // Statistics
  RegistrationStatistics reg_stats_;

  // Frames that have been filtered in current reconstruction.
  std::unordered_set<frame_t> filtered_frames_;

  // Frames that were registered before beginning the reconstruction.
  // This frame list will be non-empty, if the reconstruction is continued from
  // an existing reconstruction.
  std::unordered_set<frame_t> existing_frame_ids_;
};

}  // namespace colmap
