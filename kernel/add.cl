// add: c = a + b を計算します。

extern void ab(int a);

kernel void run(constant float *a, constant float *b, global float *c) {
  ab(0);

  size_t id = get_global_id(0);
  c[id] = a[id] + b[id];
}
