#include "lbkes_k_eigenvalue_solver.h"

#include "chi_log.h"

//###################################################################
/**Execute a k-eigenvalue linear boltzmann solver.*/
void lbs::DiscOrdKEigenvalueSolver::Execute()
{
  //======================================== Solve the k-eigenvalue problem
  PowerIteration();

  //======================================== Initialize the precursors
  if (options_.use_precursors)
  {
    ComputePrecursors();
    for (auto& v : precursor_new_local_) v /= k_eff_;
  }

  UpdateFieldFunctions();

  chi::log.Log()
      << "LinearBoltzmann::KEigenvalueSolver execution completed\n\n";
}