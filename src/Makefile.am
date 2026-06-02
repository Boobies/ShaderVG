lib_LTLIBRARIES = libOpenVG.la libShaderVGEGL.la
libOpenVG_la_CFLAGS = -pedantic -I$(top_builddir)/include
libOpenVG_la_SOURCES =\
	shDefs.h\
	shExtensions.h\
	shArrayBase.h\
	shArrays.h\
	shVectors.h\
	shPath.h\
	shImage.h\
	shPaint.h\
	shFont.h\
	shGeometry.h\
	shPipeline.h\
	shContext.h\
	shaders.h\
	shExtensions.c\
	shArrays.c\
	shVectors.c\
	shPath.c\
	shImage.c\
	shPaint.c\
	shFont.c\
	shGeometry.c\
	shPipeline.c\
	shParams.c\
	shContext.c\
	shaders.c\
	shVgu.c
libOpenVG_la_LIBADD = @OPENGL_LIBS@ @MATH_LIBS@

libShaderVGEGL_la_CFLAGS = -pedantic -I$(top_builddir)/include @EGL_CFLAGS@
libShaderVGEGL_la_SOURCES = shEGL.c
libShaderVGEGL_la_LIBADD = libOpenVG.la @DLOPEN_LIBS@

VG_includedir = $(includedir)/VG
VG_include_HEADERS =\
	$(top_builddir)/include/VG/openvg.h\
	$(top_builddir)/include/VG/vgu.h\
	$(top_builddir)/include/VG/vgext.h
