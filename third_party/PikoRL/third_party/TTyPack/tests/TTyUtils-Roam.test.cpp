#include <TTyUtils/TTyUtils-Roam.hpp>
#include <cassert>
int main() { ttyutils::roam::Predictor predictor; predictor.echo("x"); assert(predictor.pending()=="x"); predictor.confirm(1); assert(predictor.pending().empty()); return 0; }
