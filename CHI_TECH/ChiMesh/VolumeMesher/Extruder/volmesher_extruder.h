#ifndef _volmesher_extruder_h
#define _volmesher_extruder_h

#include "../chi_volumemesher.h"
#include "ChiMesh/Cell/cell_polygon.h"

struct MeshLayer
{
  std::string name;
  double height;
  int    sub_divisions;
  int    major_id;
  int    minor_id;
};

//###################################################################
/**An extruder mesher taking a flat surface and extruding it.*/
class chi_mesh::VolumeMesherExtruder : public chi_mesh::VolumeMesher
{
public:
  std::vector<MeshLayer*> input_layers;
  std::vector<double> vertex_layers;
  int node_z_index_incr;

  int bot_boundary_index;
  int top_boundary_index;

public:
  //02
  void Execute();
  //03
  //ReorderDOFs
  //04
  void SetupLayers(int default_layer_count=1);

  void CreateLocalAndBoundaryNodes(chi_mesh::MeshContinuumPtr template_continuum,
                                   chi_mesh::MeshContinuumPtr vol_continuum);
  //05
  chi_mesh::Vector3 ComputeTemplateCell3DCentroid(
                      chi_mesh::CellPolygon* n_template_cell,
                      chi_mesh::MeshContinuumPtr template_continuum,
                      int z_level_begin,int z_level_end);

  int GetCellPartitionIDFromCentroid(chi_mesh::Vector3& centroid,
                                     chi_mesh::SurfaceMesher* surf_mesher);

  bool IsTemplateCellNeighborToThisPartition(
    chi_mesh::CellPolygon* template_cell,
    chi_mesh::MeshContinuumPtr template_continuum,
    chi_mesh::SurfaceMesher* surf_mesher,
    int z_level, int tc_index);



  void ExtrudeCells(chi_mesh::MeshContinuumPtr template_continuum,
                    chi_mesh::MeshContinuumPtr vol_continuum);

};



#endif