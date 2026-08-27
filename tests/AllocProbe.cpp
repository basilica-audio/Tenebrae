// Scratch allocation probe - intentionally empty, and never committed.
//
// Used during development to bisect where processBlock's per-block
// allocations came from; the finding is now captured as a permanent
// assertion in RobustnessTests.cpp (T-X1) and reported in the PR.
