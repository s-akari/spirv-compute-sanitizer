// hello: Hello World をします。

kernel void run(constant float *a, constant float *b, global float *c) {
  printf("Hello from OpenCL!\n");
}
