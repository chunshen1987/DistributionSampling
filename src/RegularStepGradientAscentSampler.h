/*=========================================================================
 *
 *  Copyright 2011-2013 The University of North Carolina at Chapel Hill
 *  All rights reserved.
 *
 *  Licensed under the MADAI Software License. You may obtain a copy of
 *  this license at
 *
 *         https://madai-public.cs.unc.edu/visualization/software-license/
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#ifndef madai_RegularStepGradientAscentSampler_h_included
#define madai_RegularStepGradientAscentSampler_h_included


#include "Sampler.h"


namespace madai {

/** \class RegularStepGradientAscentSampler
 *
 * Straightforward implementation of a gradient ascent optimizing sampler.
 */
class RegularStepGradientAscentSampler : public Sampler {
public:
  RegularStepGradientAscentSampler();
  ~RegularStepGradientAscentSampler();

  Sample NextSample();

  /** Set the step size. */
  void SetStepSize( double stepSize );

  /** Get the step size. */
  double GetStepSize() const;

  /** Set search mode to minimize. */
  void Minimize();

  /** Set search mode to maximize.
   *
   * Maximization is on by default. */
  void Maximize();

  /** Get whether the maximization mode is on. */
  bool GetMaximize() const;

protected:
  /** Scaling factor applied to the gradient when taking the next
   * Sample. */
  double m_StepSize;

  /** True if the algorithm should attempt to maximize the function,
   * false if it should minimize. */
  bool m_Maximize;

}; // end class RegularStepGradientAscentSampler

} // end namespace madai

#endif // madai_RegularStepGradientAscentSampler_h_included
