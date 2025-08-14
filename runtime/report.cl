// ANSI colors
#define RESET "\033[0m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define BOLD "\033[1m"

void libscsan_report_index_out_of_bounds() {
  size_t gid = get_global_id(0);
  size_t lid = get_local_id(0);

  printf("\n" BLUE BOLD "===============================================================================\n" RESET BLUE "[ComputeSanitizer] " RED "error" RESET ": (Global #" YELLOW "%zu" RESET ", Local #" YELLOW "%zu" RESET ") Array index out of bounds\n", gid, lid);
}

void libscsan_report_local_memory_conflict(unsigned long prev_gid) {
  size_t gid = get_global_id(0);
  size_t lid = get_local_id(0);

  printf("\n" BLUE BOLD "===============================================================================\n" RESET BLUE "[ComputeSanitizer] " RED "error" RESET ": (Global #" YELLOW "%zu" RESET ", Local #" YELLOW "%zu" RESET ") Local memory conflict detected (Previously wrote by #" YELLOW "%zu" RESET ")\n", gid, lid, prev_gid);
}
