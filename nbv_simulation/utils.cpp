#include <cmath>
#include <cfloat>
#include <cassert>

using namespace std;

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

/**
 * Given a point in 3D space, compute the corresponding pixel coordinates in an image with no distortion or forward
 * distortion coefficients produced by the same camera
 *
 * @param pixel
 * @param intrin
 * @param point
 */
static void rs2_project_point_to_pixel(float pixel[2], const struct rs2_intrinsics* intrin, const float point[3])
{
    float x = point[0] / point[2], y = point[1] / point[2];

    if((intrin->model == RS2_DISTORTION_MODIFIED_BROWN_CONRADY) ||
       (intrin->model == RS2_DISTORTION_INVERSE_BROWN_CONRADY))
    {
        float r2 = x * x + y * y;
        float f = 1 + intrin->coeffs[0] * r2 + intrin->coeffs[1] * r2 * r2 + intrin->coeffs[4] * r2 * r2 * r2;
        x *= f;
        y *= f;
        float dx = x + 2 * intrin->coeffs[2] * x * y + intrin->coeffs[3] * (r2 + 2 * x * x);
        float dy = y + 2 * intrin->coeffs[3] * x * y + intrin->coeffs[2] * (r2 + 2 * y * y);
        x = dx;
        y = dy;
    }
    if(intrin->model == RS2_DISTORTION_FTHETA)
    {
        float r = sqrtf(x * x + y * y);
        if(r < FLT_EPSILON)
        {
            r = FLT_EPSILON;
        }
        float rd = (float)(1.0f / intrin->coeffs[0] * atan(2 * r * tan(intrin->coeffs[0] / 2.0f)));
        x *= rd / r;
        y *= rd / r;
    }
    if(intrin->model == RS2_DISTORTION_KANNALA_BRANDT4)
    {
        float r = sqrtf(x * x + y * y);
        if(r < FLT_EPSILON)
        {
            r = FLT_EPSILON;
        }
        float theta = atan(r);
        float theta2 = theta * theta;
        float series =
                1 + theta2 * (intrin->coeffs[0] +
                              theta2 * (intrin->coeffs[1] + theta2 * (intrin->coeffs[2] + theta2 * intrin->coeffs[3])));
        float rd = theta * series;
        x *= rd / r;
        y *= rd / r;
    }

    pixel[0] = x * intrin->fx + intrin->ppx;
    pixel[1] = y * intrin->fy + intrin->ppy;
}

/**
 * Given pixel coordinates and depth in an image with no distortion or inverse distortion coefficients, compute the
 * corresponding point in 3D space relative to the same camera
 *
 * @param point
 * @param intrin
 * @param pixel
 * @param depth
 */
static void rs2_deproject_pixel_to_point(float point[3],
                                         const struct rs2_intrinsics* intrin,
                                         const float pixel[2],
                                         float depth)
{
    assert(intrin->model != RS2_DISTORTION_MODIFIED_BROWN_CONRADY); // Cannot deproject from a forward-distorted image
    // assert(intrin->model != RS2_DISTORTION_BROWN_CONRADY); // Cannot deproject to an brown conrady model

    float x = (pixel[0] - intrin->ppx) / intrin->fx;
    float y = (pixel[1] - intrin->ppy) / intrin->fy;
    if(intrin->model == RS2_DISTORTION_INVERSE_BROWN_CONRADY)
    {
        float r2 = x * x + y * y;
        float f = 1 + intrin->coeffs[0] * r2 + intrin->coeffs[1] * r2 * r2 + intrin->coeffs[4] * r2 * r2 * r2;
        float ux = x * f + 2 * intrin->coeffs[2] * x * y + intrin->coeffs[3] * (r2 + 2 * x * x);
        float uy = y * f + 2 * intrin->coeffs[3] * x * y + intrin->coeffs[2] * (r2 + 2 * y * y);
        x = ux;
        y = uy;
    }
    if(intrin->model == RS2_DISTORTION_KANNALA_BRANDT4)
    {
        float rd = sqrtf(x * x + y * y);
        if(rd < FLT_EPSILON)
        {
            rd = FLT_EPSILON;
        }

        float theta = rd;
        float theta2 = rd * rd;
        for(int i = 0; i < 4; i++)
        {
            float f = theta * (1 + theta2 * (intrin->coeffs[0] +
                                             theta2 * (intrin->coeffs[1] +
                                                       theta2 * (intrin->coeffs[2] + theta2 * intrin->coeffs[3])))) -
                      rd;
            if(abs(f) < FLT_EPSILON)
            {
                break;
            }
            float df = 1 + theta2 * (3 * intrin->coeffs[0] +
                                     theta2 * (5 * intrin->coeffs[1] +
                                               theta2 * (7 * intrin->coeffs[2] + 9 * theta2 * intrin->coeffs[3])));
            theta -= f / df;
            theta2 = theta * theta;
        }
        float r = tan(theta);
        x *= r / rd;
        y *= r / rd;
    }
    if(intrin->model == RS2_DISTORTION_FTHETA)
    {
        float rd = sqrtf(x * x + y * y);
        if(rd < FLT_EPSILON)
        {
            rd = FLT_EPSILON;
        }
        float r = (float)(tan(intrin->coeffs[0] * rd) / atan(2 * tan(intrin->coeffs[0] / 2.0f)));
        x *= r / rd;
        y *= r / rd;
    }

    point[0] = depth * x;
    point[1] = depth * y;
    point[2] = depth;
}