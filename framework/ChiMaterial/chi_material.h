#ifndef CHITECH_CHI_MATERIAL_H
#define CHITECH_CHI_MATERIAL_H

#include "ChiObject/chi_object.h"

namespace chi_objects
{

/**A generic material object used to group together multiple properties.*/
class Material : public ChiObject
{
private:
  std::string name_;
public:
  static InputParameters GetInputParameters();
  explicit Material(const chi_objects::InputParameters& params);


};

}

#endif // CHITECH_CHI_MATERIAL_H
