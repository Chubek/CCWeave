Here are a list of ten semi-complex algorithms. You must implement these for all the `interpreters` and `compilers` under `examples-salvo/semi-complex`.

1. **Dijkstra’s Algorithm**
   - Finds shortest paths from one source node to all other nodes in a weighted graph with non-negative edge weights.
   - Key concepts: graphs, priority queues, greedy algorithms.

2. **A* Search Algorithm**
   - Finds an efficient shortest path using both the distance already traveled and a heuristic estimate to the destination.
   - Commonly used in pathfinding and games.
   - Key concepts: heuristics, priority queues, graph traversal.

3. **Kruskal’s Minimum Spanning Tree Algorithm**
   - Connects all graph vertices with the minimum total edge cost, without cycles.
   - Key concepts: sorting, greedy choice, Union-Find / Disjoint Set Union.

4. **Topological Sorting**
   - Produces an ordering of tasks in a directed acyclic graph (DAG), where prerequisites occur before dependent tasks.
   - Useful for dependency resolution and scheduling.
   - Key concepts: DFS or indegrees, queues, cycle detection.

5. **Knuth–Morris–Pratt (KMP) String Matching**
   - Searches for a pattern inside text efficiently by avoiding repeated comparisons.
   - Key concepts: prefix-function / LPS array, string processing.
   - Time complexity: \(O(n + m)\).

6. **Longest Increasing Subsequence (LIS)**
   - Finds the longest subsequence of numbers that is strictly increasing.
   - Can be implemented with dynamic programming in \(O(n^2)\), or binary search optimization in \(O(n\log n)\).
   - Key concepts: dynamic programming, binary search.

7. **0/1 Knapsack Problem**
   - Chooses items to maximize value under a fixed weight capacity, where each item is either selected once or not selected.
   - Key concepts: dynamic programming, optimization.
   - Typical complexity: \(O(nW)\), where \(W\) is capacity.

8. **Floyd–Warshall Algorithm**
   - Computes shortest paths between every pair of vertices in a weighted graph.
   - Can handle negative edge weights, but not negative cycles.
   - Key concepts: dynamic programming on graphs.
   - Time complexity: \(O(V^3)\).

9. **Merge Sort with Inversion Counting**
   - Sorts an array while counting inversions: pairs \((i,j)\) where \(i<j\) but \(a_i>a_j\).
   - Key concepts: divide and conquer, modified merge sort.
   - Time complexity: \(O(n\log n)\).

10. **Backtracking Sudoku Solver**
   - Solves a Sudoku grid by trying valid candidate values and undoing choices that lead to dead ends.
   - Key concepts: recursion, constraint checking, backtracking.
   - Useful extension: choose the empty cell with the fewest possible candidates first.

