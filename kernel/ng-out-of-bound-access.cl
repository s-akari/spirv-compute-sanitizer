// ng-out-of-bound-access: ホストから提供された配列のサイズを超えるアクセスを行い、不具合を誘発させます。

kernel void run(constant float *a, constant float *b, global float *c) {
  int id = get_global_id(0);
  c[id] = a[id] + b[id + 3];
}
