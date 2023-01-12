#include "lbs_linear_boltzmann_solver.h"

#include "Groupset/lbs_groupset.h"

#include "Acceleration/diffusion_mip.h"

//###################################################################
/**Initializes the Within-Group DSA solver. */
void lbs::SteadySolver::InitWGDSA(LBSGroupset& groupset)
{
  if (groupset.apply_wgdsa)
  {
    //=========================================== Make UnknownManager
    const size_t gs_G = groupset.groups.size();
    chi_math::UnknownManager uk_man;
    uk_man.AddUnknown(chi_math::UnknownType::VECTOR_N, gs_G);

    //=========================================== Make boundary conditions
    typedef chi_mesh::sweep_management::BoundaryType SwpBndryType;
    typedef lbs::acceleration::BoundaryCondition BC;
    typedef lbs::acceleration::BCType BCType;

    std::vector<BC> bcs;
    size_t bid = 0;
    for (auto& lbs_bndry : sweep_boundaries)
    {
      if (lbs_bndry->Type() == SwpBndryType::REFLECTING)
        bcs.push_back({BCType::ROBIN,{0.0,1.0,0.0}});
      else//dirichlet
        bcs.push_back({BCType::DIRICHLET,{0.0,0.0,0.0}});
      ++bid;
    }

    //=========================================== Make xs map
    typedef lbs::acceleration::Multigroup_D_and_sigR MGXs;
    typedef std::map<int, lbs::acceleration::Multigroup_D_and_sigR> MapMatID2XS;
    MapMatID2XS map_mat_id_2_mgxs;
    for (const auto& mat_id_xs_pair : matid_to_xs_map)
    {
      const auto& mat_id = mat_id_xs_pair.first;
      const auto& xs     = mat_id_xs_pair.second;

      std::vector<double> Dg  (gs_G, 0.0);
      std::vector<double> sigR(gs_G, 0.0);

      size_t g = 0;
      for (size_t gprime=groupset.groups.front().id;
           gprime<=groupset.groups.back().id; ++gprime)
      {
        Dg[g]   = xs->diffusion_coeff[gprime];
        sigR[g] = xs->sigma_removal[gprime];
        ++g;
      }//for g

      map_mat_id_2_mgxs.insert(std::make_pair(mat_id,MGXs{Dg,sigR}));
    }

    //=========================================== Create solver
    const auto& sdm = *discretization;

    auto solver =
      std::make_shared<acceleration::DiffusionMIPSolver>(
        std::string(TextName()+"_WGDSA"),
        *grid,sdm,
        uk_man,
        bcs,
        map_mat_id_2_mgxs,
        unit_cell_matrices,
        true); //verbosity

    solver->options.residual_tolerance        = groupset.wgdsa_tol;
    solver->options.max_iters                 = groupset.wgdsa_max_iters;
    solver->options.verbose                   = groupset.wgdsa_verbose;
    solver->options.additional_options_string = groupset.wgdsa_string;

    solver->Initialize();

    delta_phi_local.assign(sdm.GetNumLocalDOFs(uk_man),0.0);

    solver->AssembleAand_b(delta_phi_local);

    delta_phi_local.resize(0);
    delta_phi_local.shrink_to_fit();

    groupset.wgdsa_solver = solver;
  }
}

//###################################################################
/**Cleans up memory consuming items. */
void lbs::SteadySolver::CleanUpWGDSA(LBSGroupset& groupset)
{
  if (groupset.apply_wgdsa) groupset.wgdsa_solver = nullptr;
}

//###################################################################
/**Assembles a delta-phi vector on the first moment.*/
void lbs::SteadySolver::
  AssembleWGDSADeltaPhiVector(LBSGroupset& groupset,
                              const std::vector<double>&ref_phi_old,
                              const std::vector<double>&ref_phi_new)
{
  const auto& sdm = *discretization;
  const auto& dphi_uk_man = groupset.wgdsa_solver->UnknownStructure();
  const auto& phi_uk_man  = flux_moments_uk_man;

  const int    gsi = groupset.groups.front().id;
  const size_t gss = groupset.groups.size();

  delta_phi_local.clear();
  delta_phi_local.assign(sdm.GetNumLocalDOFs(dphi_uk_man), 0.0);

  for (const auto& cell : grid->local_cells)
  {
    const auto& cell_mapping = sdm.GetCellMapping(cell);
    const size_t num_nodes = cell_mapping.NumNodes();
    const auto& sigma_s = matid_to_xs_map[cell.material_id]->sigma_s_gtog;

    for (size_t i=0; i < num_nodes; i++)
    {
      const int64_t dphi_map = sdm.MapDOFLocal(cell, i, dphi_uk_man, 0, 0);
      const int64_t  phi_map = sdm.MapDOFLocal(cell, i,  phi_uk_man, 0, gsi);

            double* delta_phi_mapped = &delta_phi_local[dphi_map];
      const double* phi_old_mapped   = &ref_phi_old[phi_map];
      const double* phi_new_mapped   = &ref_phi_new[phi_map];

      for (size_t g=0; g<gss; g++)
      {
        delta_phi_mapped[g] =
          sigma_s[gsi+g]*(phi_new_mapped[g] - phi_old_mapped[g]);
      }//for g
    }//for node
  }//for cell
}

//###################################################################
/**Assembles a delta-phi vector on the first moment.*/
void lbs::SteadySolver::
  AssembleWGDSADeltaPhiVector(LBSGroupset& groupset,
                              const std::vector<double>& phi_in)
{
  const auto& sdm = *discretization;
  const auto& dphi_uk_man = groupset.wgdsa_solver->UnknownStructure();
  const auto& phi_uk_man  = flux_moments_uk_man;

  const int    gsi = groupset.groups.front().id;
  const size_t gss = groupset.groups.size();

  delta_phi_local.clear();
  delta_phi_local.assign(sdm.GetNumLocalDOFs(dphi_uk_man), 0.0);

  for (const auto& cell : grid->local_cells)
  {
    const auto& cell_mapping = sdm.GetCellMapping(cell);
    const size_t num_nodes = cell_mapping.NumNodes();
    const auto& sigma_s = matid_to_xs_map[cell.material_id]->sigma_s_gtog;

    for (size_t i=0; i < num_nodes; i++)
    {
      const int64_t dphi_map = sdm.MapDOFLocal(cell, i, dphi_uk_man, 0, 0);
      const int64_t  phi_map = sdm.MapDOFLocal(cell, i,  phi_uk_man, 0, gsi);

      double* delta_phi_mapped    = &delta_phi_local[dphi_map];
      const double* phi_in_mapped = &phi_in[phi_map];

      for (size_t g=0; g<gss; g++)
      {
        delta_phi_mapped[g] = sigma_s[gsi+g] * phi_in_mapped[g];
      }//for g
    }//for node
  }//for cell
}

//###################################################################
/**DAssembles a delta-phi vector on the first moment.*/
void lbs::SteadySolver::
  DisAssembleWGDSADeltaPhiVector(LBSGroupset& groupset,
                                 std::vector<double>& ref_phi_new)
{
  const auto& sdm = *discretization;
  const auto& dphi_uk_man = groupset.wgdsa_solver->UnknownStructure();
  const auto& phi_uk_man  = flux_moments_uk_man;

  const int    gsi = groupset.groups.front().id;
  const size_t gss = groupset.groups.size();

  for (const auto& cell : grid->local_cells)
  {
    const auto& cell_mapping = sdm.GetCellMapping(cell);
    const size_t num_nodes = cell_mapping.NumNodes();

    for (size_t i=0; i < num_nodes; i++)
    {
      const int64_t dphi_map = sdm.MapDOFLocal(cell, i, dphi_uk_man, 0, 0);
      const int64_t  phi_map = sdm.MapDOFLocal(cell, i,  phi_uk_man, 0, gsi);

      const double* delta_phi_mapped = &delta_phi_local[dphi_map];
            double* phi_new_mapped   = &ref_phi_new[phi_map];

      for (int g=0; g<gss; g++)
        phi_new_mapped[g] += delta_phi_mapped[g];
    }//for dof
  }//for cell

  delta_phi_local.resize(0);
  delta_phi_local.shrink_to_fit();
}

//###################################################################
/**Executes Within Group DSA. This involves assembling the system RHS,
 * solving the system and finally adding the solution to the scalar flux.*/
void lbs::SteadySolver::
  ExecuteWGDSA(LBSGroupset &groupset,
               const std::vector<double>& ref_phi_old,
               std::vector<double>& ref_phi_new)
{
  AssembleWGDSADeltaPhiVector(groupset, ref_phi_old, ref_phi_new);
  groupset.wgdsa_solver->Assemble_b(delta_phi_local);
  groupset.wgdsa_solver->Solve(delta_phi_local);
  DisAssembleWGDSADeltaPhiVector(groupset, ref_phi_new);
}