// ng-barrier-misuse: バリアの周りに条件付きフローを設置し、同期を誤用します。エラーにはなりにくいですが、ローカルメモリを活用している場合に速度低下につながります。

kernel void run(global const float *a, global const float *b, global float *c) {
  int id = get_global_id(0);

  if (id < 4) {
    barrier(CLK_LOCAL_MEM_FENCE);
  }

  c[id] = a[id] + b[id];
}
