/** @brief Distortion model: defines how pixel coordinates should be mapped to sensor coordinates. */
typedef enum rs2_distortion
{
    RS2_DISTORTION_NONE,                   /**< Rectilinear images. No distortion compensation required. */
    RS2_DISTORTION_MODIFIED_BROWN_CONRADY, /**< Equivalent to Brown-Conrady distortion, except that tangential
                                              distortion is applied to radially distorted points */
    RS2_DISTORTION_INVERSE_BROWN_CONRADY,  /**< Equivalent to Brown-Conrady distortion, except undistorts image instead
                                              of distorting it */
    RS2_DISTORTION_FTHETA,                 /**< F-Theta fish-eye distortion model */
    RS2_DISTORTION_BROWN_CONRADY,          /**< Unmodified Brown-Conrady distortion model */
    RS2_DISTORTION_KANNALA_BRANDT4,        /**< Four parameter Kannala Brandt distortion model */
    RS2_DISTORTION_COUNT                   /**< Number of enumeration values. Not a valid input: intended to be used
                                              in for-loops. */
} rs2_distortion;

/** @brief Video stream intrinsics. */
typedef struct rs2_intrinsics
{
    int width;  /**< Width of the image in pixels */
    int height; /**< Height of the image in pixels */
    float ppx;  /**< Horizontal coordinate of the principal point of the image, as a pixel offset from the left edge */
    float ppy;  /**< Vertical coordinate of the principal point of the image, as a pixel offset from the top edge */
    float fx;   /**< Focal length of the image plane, as a multiple of pixel width */
    float fy;   /**< Focal length of the image plane, as a multiple of pixel height */
    rs2_distortion model; /**< Distortion model of the image */
    float coeffs[5];      /**< Distortion coefficients */
} rs2_intrinsics;
