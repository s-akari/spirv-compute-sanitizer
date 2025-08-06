// hello: Hello World をします。

kernel void run(global const float *a, global const float *b, global float *c) {
  printf("Hello from OpenCL!\n");
}
