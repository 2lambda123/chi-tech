#include "lbs_solver.h"

#include "ChiPhysics/PhysicsMaterial/chi_physicsmaterial.h"

#include "chi_runtime.h"
#include "chi_log.h"

#include "chi_mpi.h"


#include <algorithm>

//###################################################################
/**Initializes default materials and physics materials.*/
void lbs::LBSSolver::InitMaterials()
{
  const std::string fname = "lbs::SteadyStateSolver::InitMaterials";
  chi::log.Log0Verbose1() << "Initializing Materials";

  //================================================== Create set of material
  //                                                   ids locally relevant
  std::set<int> unique_material_ids;
  int invalid_mat_cell_count = 0;
  for (auto& cell : grid_ptr_->local_cells)
  {
    unique_material_ids.insert(cell.material_id_);
    if (cell.material_id_ < 0)
      ++invalid_mat_cell_count;
  }
  const auto& ghost_cell_ids = grid_ptr_->cells.GetGhostGlobalIDs();
  for (uint64_t cell_id : ghost_cell_ids)
  {
    const auto& cell = grid_ptr_->cells[cell_id];
    unique_material_ids.insert(cell.material_id_);
    if (cell.material_id_ < 0)
      ++invalid_mat_cell_count;
  }

  if (invalid_mat_cell_count>0)
  {
    chi::log.LogAllWarning()
      << "Number of invalid material cells: " << invalid_mat_cell_count;
  }

  //================================================== Get ready for processing
  std::stringstream materials_list;
  matid_to_xs_map_.clear();
  matid_to_src_map_.clear();

  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Process materials found
  const size_t num_physics_mats = chi::material_stack.size();

  for (const int& mat_id : unique_material_ids)
  {
    materials_list << "Material id " << mat_id;

    //====================================== Check valid ids
    if (mat_id < 0)
      throw std::logic_error(fname + ": Cells encountered with no assigned "
                                     "material.");
    if (static_cast<size_t>(mat_id) >= num_physics_mats)
      throw std::logic_error(fname + ": Cells encountered with material id that"
                                     " matches no material in physics material "
                                     "library.");

    auto current_material = chi::GetStackItemPtr(chi::material_stack,
                                                 mat_id, fname);

    //====================================== Extract properties
    using MatProperty = chi_physics::PropertyType;
    bool found_transport_xs = false;
    for (const auto& property : current_material->properties_)
    {
      if (property->Type() == MatProperty::TRANSPORT_XSECTIONS)
      {
        auto transp_xs =
          std::static_pointer_cast<chi_physics::TransportCrossSections>(property);
        matid_to_xs_map_[mat_id] = transp_xs;
        found_transport_xs = true;
      }//transport xs
      if (property->Type() == MatProperty::ISOTROPIC_MG_SOURCE)
      {
        auto mg_source =
          std::static_pointer_cast<chi_physics::IsotropicMultiGrpSource>(property);

        if (mg_source->source_value_g_.size() < groups_.size())
        {
          chi::log.LogAllWarning()
            << fname + ": Isotropic Multigroup source specified in "
            << "material \"" << current_material->name_ << "\" has fewer "
            << "energy groups than called for in the simulation. "
            << "Source will be ignored.";
        }
        else
        {
          matid_to_src_map_[mat_id] = mg_source;
        }
      }//P0 source
    }//for property

    //====================================== Check valid property
    if (!found_transport_xs)
    {
      chi::log.LogAllError()
        << fname + ": Found no transport cross-section property for "
        << "material \"" << current_material->name_ << "\".";
      chi::Exit(EXIT_FAILURE);
    }
    //====================================== Check number of groups legal
    if (matid_to_xs_map_[mat_id]->num_groups_ < groups_.size())
    {
      chi::log.LogAllError()
          << fname + ": Found material \"" << current_material->name_ << "\" has "
          << matid_to_xs_map_[mat_id]->num_groups_ << " groups and"
          << " the simulation has " << groups_.size() << " groups."
          << " The material must have a greater or equal amount of groups.";
      chi::Exit(EXIT_FAILURE);
    }

    //====================================== Check number of moments
    if (matid_to_xs_map_[mat_id]->scattering_order_ < options_.scattering_order)
    {
      chi::log.Log0Warning()
          << fname + ": Found material \"" << current_material->name_ << "\" has "
          << "a scattering order of "
          << matid_to_xs_map_[mat_id]->scattering_order_ << " and"
          << " the simulation has a scattering order of "
          << options_.scattering_order << "."
          << " The higher moments will therefore not be used.";
    }

    materials_list
        << " number of moments "
        << matid_to_xs_map_[mat_id]->transfer_matrices_.size() << "\n";
  }//for material id

  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Initialize precursor
  //                                                   properties

  num_precursors_ = 0;
  max_precursors_per_material_ = 0;
  for (const auto& mat_id_xs : matid_to_xs_map_)
  {
    const auto& xs = mat_id_xs.second;
    num_precursors_ += xs->num_precursors_;
    if (xs->num_precursors_ > max_precursors_per_material_)
      max_precursors_per_material_ = xs->num_precursors_;
  }

  //if no precursors, turn off precursors
  if (num_precursors_ == 0)
    options_.use_precursors = false;

  //check compatibility when precursors are on
  if (options_.use_precursors)
  {
    for (const auto& mat_id_xs: matid_to_xs_map_)
    {
      const auto& xs = mat_id_xs.second;
      if (xs->is_fissionable_ && num_precursors_ == 0)
        throw std::logic_error(
            "Incompatible cross section data encountered."
            "When delayed neutron data is present for one "
            "fissionable material, it must be present for "
            "all fissionable materials.");
    }
  }


  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Initialize Diffusion
  //                                                   properties
  bool develop_diffusion_properties = false;
  for (auto& group_set : groupsets_)
  {
    if (group_set.apply_wgdsa_ || group_set.apply_tgdsa_)
      develop_diffusion_properties = true;
  }

  if (develop_diffusion_properties)
  {
    chi::log.Log() << "Computing diffusion parameters.";

    for (const auto& mat_id_xs : matid_to_xs_map_)
      mat_id_xs.second->ComputeDiffusionParameters();
  }

  //================================================== Update transport views
  //                                                   if available
  if (grid_ptr_->local_cells.size() == cell_transport_views_.size())
    for (const auto& cell : grid_ptr_->local_cells)
    {
      const auto& xs_ptr = matid_to_xs_map_[cell.material_id_];
      auto& transport_view = cell_transport_views_[cell.local_id_];

      transport_view.ReassingXS(*xs_ptr);
    }

  chi::log.Log0Verbose1()
    << "Materials Initialized:\n" << materials_list.str() << "\n";

  MPI_Barrier(MPI_COMM_WORLD);
}
