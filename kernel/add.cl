// add: c = a + b を計算します。

kernel void run(global const float *a, global const float *b, global float *c) {
  int id = get_global_id(0);
  c[id] = a[id] + b[id];
}
