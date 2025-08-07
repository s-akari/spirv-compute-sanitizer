// in-out-size: 適当な int in[ARRAY_SIZE], out[ARRAY_SIZE] を作成し、run(in,
// out, ARRAY_SIZE) を呼び出します。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cl.h"

#define ARRAY_SIZE 256

typedef struct {
  int h_in[ARRAY_SIZE];
  int h_out[ARRAY_SIZE];
  cl_mem d_in;
  cl_mem d_out;
} OpenCLBuffers;

int init_opencl_buffers(OpenCLContext *ctx, OpenCLBuffers *buffers) {
  cl_int err;

  for (int i = 0; i < ARRAY_SIZE; ++i) {
    buffers->h_in[i] = i + 1;
  }

  memset(buffers->h_out, 0, sizeof(buffers->h_out));

  const size_t buffer_size = sizeof(int) * ARRAY_SIZE;

  // Create buffers
  buffers->d_in =
      clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     buffer_size, buffers->h_in, &err);
  CHECK_CL_ERROR(err, "Error in creating input buffer");

  buffers->d_out =
      clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY, buffer_size, NULL, &err);
  CHECK_CL_ERROR(err, "Error in creating output buffer");

  return 0;
}

void clean_opencl_buffers(OpenCLBuffers *buffers) {
  if (buffers->d_in)
    clReleaseMemObject(buffers->d_in);
  if (buffers->d_out)
    clReleaseMemObject(buffers->d_out);

  memset(buffers, 0, sizeof(OpenCLBuffers));
}

void print_array(const char *name, const size_t size, const int array[]) {
  for (size_t i = 0; i < size; ++i) {
    printf("%s[%zu] = %d\n", name, i, array[i]);
  }
}

int run_kernel(OpenCLContext *ctx, OpenCLBuffers *buffers) {
  cl_int err;

  // Set kernel arguments
  err = clSetKernelArg(ctx->kernel, 0, sizeof(cl_mem), &buffers->d_in);
  CHECK_CL_ERROR(err, "Error in setting kernel argument d_in");

  err = clSetKernelArg(ctx->kernel, 1, sizeof(cl_mem), &buffers->d_out);
  CHECK_CL_ERROR(err, "Error in setting kernel argument d_out");

  const int size = ARRAY_SIZE;

  err = clSetKernelArg(ctx->kernel, 2, sizeof(int), &size);
  CHECK_CL_ERROR(err, "Error in setting kernel argument size");

  // Enqueue kernel
  const size_t global_size = ARRAY_SIZE;
  const size_t local_size = 64;
  const size_t buffer_size = sizeof(int) * ARRAY_SIZE;

  err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel, 1, NULL, &global_size,
                               &local_size, 0, NULL, NULL);
  CHECK_CL_ERROR(err, "Error in enqueuing kernel");

  // Wait for the kernel to finish
  err = clFinish(ctx->queue);
  CHECK_CL_ERROR(err, "Error in finishing command queue");

  // Read the output buffer
  err = clEnqueueReadBuffer(ctx->queue, buffers->d_out, CL_TRUE, 0, buffer_size,
                            buffers->h_out, 0, NULL, NULL);
  CHECK_CL_ERROR(err, "Error in reading output buffer");

  return 0;
}

int main(const int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path_to_spv_file>\n", argv[0]);

    return EXIT_FAILURE;
  }

  const char *path = argv[1];

  OpenCLContext ctx = {0};

  if (init_opencl_context(&ctx) != 0) {
    fprintf(stderr, "OpenCL context initialization failed.\n");

    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  if (load_spv_program(&ctx, path, "run") != 0) {
    fprintf(stderr, "Cannot load SPIR-V program.\n");

    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  OpenCLBuffers buffers = {0};

  if (init_opencl_buffers(&ctx, &buffers) != 0) {
    fprintf(stderr, "Failed to initialize OpenCL buffers.\n");

    clean_opencl_buffers(&buffers);
    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  if (run_kernel(&ctx, &buffers) != 0) {
    fprintf(stderr, "Kernel execution failed.\n");

    clean_opencl_buffers(&buffers);
    clean_opencl_context(&ctx);

    return EXIT_FAILURE;
  }

  printf("Output array:\n");
  print_array("c", ARRAY_SIZE, buffers.h_out);

  clean_opencl_buffers(&buffers);
  clean_opencl_context(&ctx);

  return EXIT_SUCCESS;
}
