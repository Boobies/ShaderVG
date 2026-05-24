#include "test.h"

extern const VGint     pathCount;
extern const VGint     commandCounts[];
extern const VGubyte*  commandArrays[];
extern const VGfloat*  dataArrays[];
extern const VGfloat*  styleArrays[];

#define TIGER_IMAGE_WIDTH  540
#define TIGER_IMAGE_HEIGHT 580
#define TIGER_IMAGE_TX     300.0f
#define TIGER_IMAGE_TY     292.0f

static VGPath *tigerPaths = NULL;
static VGPaint tigerStroke = VG_INVALID_HANDLE;
static VGPaint tigerFill = VG_INVALID_HANDLE;
static VGPaint outlinePaint = VG_INVALID_HANDLE;
static VGImage tigerImage = VG_INVALID_HANDLE;

static VGPath referenceOutline = VG_INVALID_HANDLE;
static VGPath squareWarpOutline = VG_INVALID_HANDLE;
static VGPath quadToSquareOutline = VG_INVALID_HANDLE;
static VGPath quadWarpOutline = VG_INVALID_HANDLE;

static const VGfloat referenceQuad[8] = {
  55.0f, 340.0f,
  285.0f, 340.0f,
  55.0f, 585.0f,
  285.0f, 585.0f
};

static const VGfloat squareWarpQuad[8] = {
  390.0f, 338.0f,
  715.0f, 380.0f,
  350.0f, 590.0f,
  750.0f, 535.0f
};

static const VGfloat sourceQuad[8] = {
  0.0f, 0.0f,
  (VGfloat)TIGER_IMAGE_WIDTH, 0.0f,
  0.0f, (VGfloat)TIGER_IMAGE_HEIGHT,
  (VGfloat)TIGER_IMAGE_WIDTH, (VGfloat)TIGER_IMAGE_HEIGHT
};

static const VGfloat quadToSquareQuad[8] = {
  60.0f, 58.0f,
  310.0f, 92.0f,
  35.0f, 296.0f,
  335.0f, 255.0f
};

static const VGfloat quadWarpQuad[8] = {
  430.0f, 48.0f,
  765.0f, 78.0f,
  390.0f, 295.0f,
  800.0f, 260.0f
};

static void set_paint_color(VGPaint paint,
                            VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  VGfloat color[4];
  color[0] = r;
  color[1] = g;
  color[2] = b;
  color[3] = a;
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
}

static VGImageFormat native_rgba_format(void)
{
  unsigned int littleEndianTest = 1;

  if (((unsigned char*)&littleEndianTest)[0] == 1)
    return VG_sABGR_8888;

  return VG_sRGBA_8888;
}

static void multiply_matrices(const VGfloat *left,
                              const VGfloat *right,
                              VGfloat *product)
{
  int row, column;

  for (row=0; row<3; ++row) {
    for (column=0; column<3; ++column) {
      product[column * 3 + row] =
        left[row] * right[column * 3] +
        left[3 + row] * right[column * 3 + 1] +
        left[6 + row] * right[column * 3 + 2];
    }
  }
}

static VGPath create_quad_path(const VGfloat *quad)
{
  VGfloat outline[8];
  VGPath path = testCreatePath();

  if (path == VG_INVALID_HANDLE)
    return VG_INVALID_HANDLE;

  outline[0] = quad[0]; outline[1] = quad[1];
  outline[2] = quad[2]; outline[3] = quad[3];
  outline[4] = quad[6]; outline[5] = quad[7];
  outline[6] = quad[4]; outline[7] = quad[5];

  if (vguPolygon(path, outline, 4, VG_TRUE) != VGU_NO_ERROR) {
    vgDestroyPath(path);
    return VG_INVALID_HANDLE;
  }

  return path;
}

static int load_tiger_paths(void)
{
  VGPath temp;
  int i;

  tigerPaths = (VGPath*)calloc((size_t)pathCount, sizeof(VGPath));
  if (!tigerPaths)
    return 0;

  temp = testCreatePath();
  if (temp == VG_INVALID_HANDLE)
    return 0;

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(-100.0f, 100.0f);
  vgScale(1.0f, -1.0f);

  for (i=0; i<pathCount; ++i) {
    tigerPaths[i] = testCreatePath();
    if (tigerPaths[i] == VG_INVALID_HANDLE) {
      vgDestroyPath(temp);
      return 0;
    }

    vgClearPath(temp, VG_PATH_CAPABILITY_ALL);
    vgAppendPathData(temp, commandCounts[i],
                     commandArrays[i], dataArrays[i]);
    vgTransformPath(tigerPaths[i], temp);
  }

  vgLoadIdentity();
  vgDestroyPath(temp);

  tigerStroke = vgCreatePaint();
  tigerFill = vgCreatePaint();
  if (tigerStroke == VG_INVALID_HANDLE || tigerFill == VG_INVALID_HANDLE)
    return 0;

  return 1;
}

static void draw_tiger_paths(void)
{
  int i;
  const VGfloat *style;

  vgSetPaint(tigerStroke, VG_STROKE_PATH);
  vgSetPaint(tigerFill, VG_FILL_PATH);

  for (i=0; i<pathCount; ++i) {
    style = styleArrays[i];
    vgSetParameterfv(tigerStroke, VG_PAINT_COLOR, 4, &style[0]);
    vgSetParameterfv(tigerFill, VG_PAINT_COLOR, 4, &style[4]);
    vgSetf(VG_STROKE_LINE_WIDTH, style[8]);
    vgDrawPath(tigerPaths[i], (VGint)style[9]);
  }
}

static int create_tiger_image(void)
{
  unsigned char *pixels;
  VGfloat transparent[] = {0.0f, 0.0f, 0.0f, 0.0f};

  pixels = (unsigned char*)malloc((size_t)TIGER_IMAGE_WIDTH *
                                  (size_t)TIGER_IMAGE_HEIGHT * 4u);
  if (!pixels)
    return 0;

  tigerImage = vgCreateImage(native_rgba_format(),
                             TIGER_IMAGE_WIDTH,
                             TIGER_IMAGE_HEIGHT,
                             VG_IMAGE_QUALITY_BETTER);
  if (tigerImage == VG_INVALID_HANDLE) {
    free(pixels);
    return 0;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, transparent);
  vgClear(0, 0, testWidth(), testHeight());

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(TIGER_IMAGE_TX, TIGER_IMAGE_TY);
  draw_tiger_paths();
  vgFinish();

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, TIGER_IMAGE_WIDTH, TIGER_IMAGE_HEIGHT,
               GL_RGBA, GL_UNSIGNED_BYTE, pixels);

  vgImageSubData(tigerImage, pixels, TIGER_IMAGE_WIDTH * 4,
                 native_rgba_format(), 0, 0,
                 TIGER_IMAGE_WIDTH, TIGER_IMAGE_HEIGHT);

  free(pixels);
  return vgGetError() == VG_NO_ERROR;
}

static int create_scene_resources(void)
{
  outlinePaint = vgCreatePaint();
  if (outlinePaint == VG_INVALID_HANDLE)
    return 0;

  set_paint_color(outlinePaint, 1.0f, 1.0f, 1.0f, 0.95f);

  referenceOutline = create_quad_path(referenceQuad);
  squareWarpOutline = create_quad_path(squareWarpQuad);
  quadToSquareOutline = create_quad_path(quadToSquareQuad);
  quadWarpOutline = create_quad_path(quadWarpQuad);

  return referenceOutline != VG_INVALID_HANDLE &&
         squareWarpOutline != VG_INVALID_HANDLE &&
         quadToSquareOutline != VG_INVALID_HANDLE &&
         quadWarpOutline != VG_INVALID_HANDLE;
}

static void draw_image_with_matrix(const VGfloat *matrix)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadMatrix(matrix);
  vgDrawImage(tigerImage);
}

static void draw_reference_image(void)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(referenceQuad[0], referenceQuad[1]);
  vgScale((referenceQuad[2] - referenceQuad[0]) / TIGER_IMAGE_WIDTH,
          (referenceQuad[5] - referenceQuad[1]) / TIGER_IMAGE_HEIGHT);
  vgDrawImage(tigerImage);
}

static void draw_square_to_quad_image(void)
{
  VGfloat matrix[9];

  if (vguComputeWarpSquareToQuad(squareWarpQuad[0], squareWarpQuad[1],
                                 squareWarpQuad[2], squareWarpQuad[3],
                                 squareWarpQuad[4], squareWarpQuad[5],
                                 squareWarpQuad[6], squareWarpQuad[7],
                                 matrix) != VGU_NO_ERROR)
    return;

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadMatrix(matrix);
  vgScale(1.0f / TIGER_IMAGE_WIDTH, 1.0f / TIGER_IMAGE_HEIGHT);
  vgDrawImage(tigerImage);
}

static void draw_rectified_image(void)
{
  VGfloat sourceToSquare[9];
  VGfloat squareToDestination[9];
  VGfloat imageToSurface[9];

  if (vguComputeWarpQuadToSquare(sourceQuad[0], sourceQuad[1],
                                 sourceQuad[2], sourceQuad[3],
                                 sourceQuad[4], sourceQuad[5],
                                 sourceQuad[6], sourceQuad[7],
                                 sourceToSquare) != VGU_NO_ERROR)
    return;

  if (vguComputeWarpSquareToQuad(quadToSquareQuad[0], quadToSquareQuad[1],
                                 quadToSquareQuad[2], quadToSquareQuad[3],
                                 quadToSquareQuad[4], quadToSquareQuad[5],
                                 quadToSquareQuad[6], quadToSquareQuad[7],
                                 squareToDestination) != VGU_NO_ERROR)
    return;

  multiply_matrices(squareToDestination, sourceToSquare, imageToSurface);
  draw_image_with_matrix(imageToSurface);
}

static void draw_quad_to_quad_image(void)
{
  VGfloat matrix[9];

  if (vguComputeWarpQuadToQuad(quadWarpQuad[0], quadWarpQuad[1],
                               quadWarpQuad[2], quadWarpQuad[3],
                               quadWarpQuad[4], quadWarpQuad[5],
                               quadWarpQuad[6], quadWarpQuad[7],
                               0.0f, 0.0f,
                               (VGfloat)TIGER_IMAGE_WIDTH, 0.0f,
                               0.0f, (VGfloat)TIGER_IMAGE_HEIGHT,
                               (VGfloat)TIGER_IMAGE_WIDTH,
                               (VGfloat)TIGER_IMAGE_HEIGHT,
                               matrix) != VGU_NO_ERROR)
    return;

  draw_image_with_matrix(matrix);
}

static void draw_outline(VGPath path, VGPaint paint, VGfloat width)
{
  vgSetPaint(paint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, width);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgDrawPath(path, VG_STROKE_PATH);
}

static void display(float interval)
{
  VGfloat clearColor[] = {0.10f, 0.11f, 0.13f, 1.0f};

  (void)interval;

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, testWidth(), testHeight());

  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_BETTER);

  draw_reference_image();
  draw_square_to_quad_image();
  draw_rectified_image();
  draw_quad_to_quad_image();

  draw_outline(referenceOutline, outlinePaint, 1.5f);
  draw_outline(squareWarpOutline, outlinePaint, 1.5f);
  draw_outline(quadToSquareOutline, outlinePaint, 1.5f);
  draw_outline(quadWarpOutline, outlinePaint, 1.5f);
}

static void destroy_path(VGPath *path)
{
  if (*path != VG_INVALID_HANDLE) {
    vgDestroyPath(*path);
    *path = VG_INVALID_HANDLE;
  }
}

static void cleanup(void)
{
  int i;

  destroy_path(&referenceOutline);
  destroy_path(&squareWarpOutline);
  destroy_path(&quadToSquareOutline);
  destroy_path(&quadWarpOutline);

  if (tigerImage != VG_INVALID_HANDLE) {
    vgDestroyImage(tigerImage);
    tigerImage = VG_INVALID_HANDLE;
  }

  if (outlinePaint != VG_INVALID_HANDLE) {
    vgDestroyPaint(outlinePaint);
    outlinePaint = VG_INVALID_HANDLE;
  }

  if (tigerStroke != VG_INVALID_HANDLE) {
    vgDestroyPaint(tigerStroke);
    tigerStroke = VG_INVALID_HANDLE;
  }

  if (tigerFill != VG_INVALID_HANDLE) {
    vgDestroyPaint(tigerFill);
    tigerFill = VG_INVALID_HANDLE;
  }

  if (tigerPaths) {
    for (i=0; i<pathCount; ++i) {
      if (tigerPaths[i] != VG_INVALID_HANDLE)
        vgDestroyPath(tigerPaths[i]);
    }
    free(tigerPaths);
    tigerPaths = NULL;
  }
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 920, 640, "ShaderVG: Image Warp Test");
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);

  if (!load_tiger_paths() ||
      !create_tiger_image() ||
      !create_scene_resources()) {
    fprintf(stderr, "Failed to initialize image warp example\n");
    cleanup();
    return EXIT_FAILURE;
  }

  testRun();

  return EXIT_SUCCESS;
}
