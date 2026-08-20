set(CEPHYR_SEMI_COMPLEX
  01_dijkstra.c 02_astar.c 03_kruskal.c 04_topological_sort.c
  05_kmp.c 06_lis.c 07_knapsack.c 08_floyd_warshall.c
  09_merge_inversions.c 10_sudoku.c)
add_custom_target(cephyr_semi_complex SOURCES ${CEPHYR_SEMI_COMPLEX})
