#include "test.h"

#define IMAGE_W 160
#define IMAGE_H 160
#define PANEL_W 168
#define PANEL_H 168
#define PANEL_MARGIN 4
#define PANEL_GAP 24
#define PANEL_COLS 3
#define PANEL_ROWS 2

static VGImage sourceImage = VG_INVALID_HANDLE;
static VGImage colorMatrixImage = VG_INVALID_HANDLE;
static VGImage lookupImage = VG_INVALID_HANDLE;
static VGImage convolveImage = VG_INVALID_HANDLE;
static VGImage separableImage = VG_INVALID_HANDLE;
static VGImage gaussianImage = VG_INVALID_HANDLE;
static VGPath panelPath = VG_INVALID_HANDLE;
static VGPaint fillPaint = VG_INVALID_HANDLE;
static VGPaint strokePaint = VG_INVALID_HANDLE;

static VGfloat firstPanelX(void)
{
  return (testWidth() -
          (PANEL_W * PANEL_COLS + PANEL_GAP * (PANEL_COLS - 1))) * 0.5f;
}

static VGfloat firstPanelY(void)
{
  return (testHeight() -
          (PANEL_H * PANEL_ROWS + PANEL_GAP * (PANEL_ROWS - 1))) * 0.5f;
}

static void setPaintColor(VGPaint paint,
                          VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  VGfloat color[4];
  color[0] = r;
  color[1] = g;
  color[2] = b;
  color[3] = a;
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
}

static void setPixel(VGubyte *data,
                     VGint x, VGint y,
                     VGubyte r, VGubyte g, VGubyte b, VGubyte a)
{
  size_t offset = ((size_t)y * IMAGE_W + (size_t)x) * 4u;

  data[offset + 0] = r;
  data[offset + 1] = g;
  data[offset + 2] = b;
  data[offset + 3] = a;
}

static VGubyte clampByte(VGint value)
{
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (VGubyte)value;
}

static void createSourcePixels(VGubyte *data)
{
  VGint x;
  VGint y;

  for (y=0; y<IMAGE_H; ++y) {
    for (x=0; x<IMAGE_W; ++x) {
      VGint checker = ((x / 16) + (y / 16)) & 1;
      VGint dx = x - IMAGE_W / 2;
      VGint dy = y - IMAGE_H / 2;
      VGint r = (x * 255) / (IMAGE_W - 1);
      VGint g = (y * 255) / (IMAGE_H - 1);
      VGint b = checker ? 74 : 172;
      VGint diagonal = y - x;

      if (dx * dx + dy * dy < 40 * 40) {
        r = 248;
        g = 172;
        b = 32;
      }

      if (diagonal > -4 && diagonal < 4) {
        r = 28;
        g = 206;
        b = 224;
      }

      if (x > 106 && y < 54) {
        r = 42;
        g = 64;
        b = 210;
      }

      if (x < 48 && y > 112) {
        r = 238;
        g = 74;
        b = 82;
      }

      setPixel(data, x, y,
               clampByte(r), clampByte(g), clampByte(b), 255);
    }
  }
}

static void createLookupTables(VGubyte *redLut,
                               VGubyte *greenLut,
                               VGubyte *blueLut,
                               VGubyte *alphaLut)
{
  VGint i;

  for (i=0; i<256; ++i) {
    redLut[i] = (VGubyte)(255 - i);
    greenLut[i] = (VGubyte)((i / 64) * 85);
    blueLut[i] = (VGubyte)((i < 128) ? 42 : 230);
    alphaLut[i] = 255;
  }
}

static VGImage createImage(void)
{
  return vgCreateImage(VG_lABGR_8888, IMAGE_W, IMAGE_H,
                       VG_IMAGE_QUALITY_BETTER);
}

static void createFilterImages(void)
{
  VGubyte *sourceData;
  VGubyte redLut[256];
  VGubyte greenLut[256];
  VGubyte blueLut[256];
  VGubyte alphaLut[256];
  VGfloat sepiaMatrix[20] = {
    0.393f, 0.349f, 0.272f, 0.0f,
    0.769f, 0.686f, 0.534f, 0.0f,
    0.189f, 0.168f, 0.131f, 0.0f,
    0.0f,   0.0f,   0.0f,   1.0f,
    0.0f,   0.0f,   0.0f,   0.0f
  };
  VGshort sharpenKernel[9] = {
    0, -1, 0,
    -1, 5, -1,
    0, -1, 0
  };
  VGshort blurKernel[3] = {1, 2, 1};
  VGfloat tileFillColor[4] = {0.05f, 0.06f, 0.07f, 1.0f};

  sourceData = (VGubyte*)malloc(IMAGE_W * IMAGE_H * 4);
  if (!sourceData)
    return;

  createSourcePixels(sourceData);
  createLookupTables(redLut, greenLut, blueLut, alphaLut);

  sourceImage = createImage();
  colorMatrixImage = createImage();
  lookupImage = createImage();
  convolveImage = createImage();
  separableImage = createImage();
  gaussianImage = createImage();

  if (sourceImage == VG_INVALID_HANDLE ||
      colorMatrixImage == VG_INVALID_HANDLE ||
      lookupImage == VG_INVALID_HANDLE ||
      convolveImage == VG_INVALID_HANDLE ||
      separableImage == VG_INVALID_HANDLE ||
      gaussianImage == VG_INVALID_HANDLE) {
    free(sourceData);
    return;
  }

  vgImageSubData(sourceImage, sourceData, IMAGE_W * 4,
                 VG_lABGR_8888, 0, 0, IMAGE_W, IMAGE_H);
  free(sourceData);

  vgSeti(VG_FILTER_FORMAT_LINEAR, VG_TRUE);
  vgSeti(VG_FILTER_FORMAT_PREMULTIPLIED, VG_FALSE);
  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);
  vgSetfv(VG_TILE_FILL_COLOR, 4, tileFillColor);

  vgColorMatrix(colorMatrixImage, sourceImage, sepiaMatrix);
  vgLookup(lookupImage, sourceImage,
           redLut, greenLut, blueLut, alphaLut, VG_TRUE, VG_FALSE);
  vgConvolve(convolveImage, sourceImage,
             3, 3, 1, 1, sharpenKernel, 1.0f, 0.0f, VG_TILE_PAD);
  vgSeparableConvolve(separableImage, sourceImage,
                      3, 3, 1, 1,
                      blurKernel, blurKernel,
                      1.0f / 16.0f, 0.0f, VG_TILE_PAD);
  vgGaussianBlur(gaussianImage, sourceImage, 2.2f, 2.2f, VG_TILE_PAD);
}

static void createContent(void)
{
  panelPath = testCreatePath();
  vguRoundRect(panelPath, 0.0f, 0.0f,
               (VGfloat)PANEL_W, (VGfloat)PANEL_H, 6.0f, 6.0f);

  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();
  createFilterImages();
}

static void drawPanel(VGfloat x, VGfloat y, VGImage image)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  setPaintColor(fillPaint, 0.08f, 0.09f, 0.11f, 1.0f);
  setPaintColor(strokePaint, 0.44f, 0.48f, 0.52f, 1.0f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 1.5f);
  vgDrawPath(panelPath, VG_FILL_PATH | VG_STROKE_PATH);

  if (image == VG_INVALID_HANDLE)
    return;

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x + PANEL_MARGIN, y + PANEL_MARGIN);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(image);
}

static void display(float interval)
{
  VGfloat clearColor[] = {0.15f, 0.17f, 0.19f, 1.0f};
  VGfloat x = firstPanelX();
  VGfloat y = firstPanelY();

  (void)interval;

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, testWidth(), testHeight());

  drawPanel(x, y + PANEL_H + PANEL_GAP, sourceImage);
  drawPanel(x + PANEL_W + PANEL_GAP, y + PANEL_H + PANEL_GAP,
            colorMatrixImage);
  drawPanel(x + (PANEL_W + PANEL_GAP) * 2, y + PANEL_H + PANEL_GAP,
            lookupImage);

  drawPanel(x, y, convolveImage);
  drawPanel(x + PANEL_W + PANEL_GAP, y, separableImage);
  drawPanel(x + (PANEL_W + PANEL_GAP) * 2, y, gaussianImage);
}

static void cleanup(void)
{
  if (gaussianImage != VG_INVALID_HANDLE)
    vgDestroyImage(gaussianImage);
  if (separableImage != VG_INVALID_HANDLE)
    vgDestroyImage(separableImage);
  if (convolveImage != VG_INVALID_HANDLE)
    vgDestroyImage(convolveImage);
  if (lookupImage != VG_INVALID_HANDLE)
    vgDestroyImage(lookupImage);
  if (colorMatrixImage != VG_INVALID_HANDLE)
    vgDestroyImage(colorMatrixImage);
  if (sourceImage != VG_INVALID_HANDLE)
    vgDestroyImage(sourceImage);
  if (panelPath != VG_INVALID_HANDLE)
    vgDestroyPath(panelPath);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
}

int main(int argc, char **argv)
{
  testInit(argc, argv,
           PANEL_W * PANEL_COLS + PANEL_GAP * (PANEL_COLS - 1) + 64,
           PANEL_H * PANEL_ROWS + PANEL_GAP * (PANEL_ROWS - 1) + 64,
           "ShaderVG: Image Filters");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  testRun();

  return EXIT_SUCCESS;
}
