#pragma once

#include <CL/cl.h>

#define CL_ERROR(err, msg)                                                     \
  do {                                                                         \
    fprintf(stderr, "OpenCL error (%d): %s\n", err, msg);                      \
    return -1;                                                                 \
  } while (0)

#define CHECK_CL_ERROR(err, msg)                                               \
  do {                                                                         \
    if (err != CL_SUCCESS) {                                                   \
      CL_ERROR(err, msg);                                                      \
    }                                                                          \
  } while (0)

typedef struct {
  cl_context context;
  cl_command_queue queue;
  cl_program program;
  cl_kernel kernel;
  cl_device_id device;
} OpenCLContext;

int init_opencl_context(OpenCLContext *ctx);

void clean_opencl_context(OpenCLContext *ctx);

int load_spv_program(OpenCLContext *ctx, const char *path,
                     const char *kernel_name);
