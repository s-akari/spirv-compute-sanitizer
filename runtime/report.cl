void libscsan_report_index_out_of_bounds() {
  size_t gid = get_global_id(0);
  size_t lid = get_local_id(0);

  printf("\n[ComputeSanitizer] (Global #%zu, Local #%zu) Array index out of bounds\n", gid, lid);
}

void libscsan_report_local_memory_conflict(unsigned long prev_gid) {
  size_t gid = get_global_id(0);
  size_t lid = get_local_id(0);

  printf("\n[ComputeSanitizer] (Global #%zu, Local #%zu) Local memory conflict detected (Previously wrote by #%zu)\n", gid, lid, prev_gid);
}
