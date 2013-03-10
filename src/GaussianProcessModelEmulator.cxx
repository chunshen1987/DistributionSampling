/*=========================================================================
 *
 *  Copyright 2011-2013 The University of North Carolina at Chapel Hill
 *  All rights reserved.
 *
 *  Licensed under the MADAI Software License. You may obtain a copy of
 *  this license at
 *
 *         https://madai-public.cs.unc.edu/software/license/
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

/*
 * GaussianProcessModelEmulator Class
 *
 * \author Hal Canary <cs.unc.edu/~hal/>
 * \author Christopher Coleman-Smith <cec24@phy.duke.edu>
 * \author Cory Quammen <cs.unc.edu/~cquammen>
 */

#include "GaussianProcessModelEmulator.h"
#include <cstdio> // remove it later
#include <iostream>
#include <cmath>
#include <limits>       // std::numeric_limits
#include "UniformDistribution.h"
#include "GaussianDistribution.h"
using std::fprintf;

using namespace madai;
namespace {

/**
   The standard clamp macro. */
template <typename T>
inline T clamp(const T & x, const T & low, const T & high) {
  return (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)));
}

/**
   Read a word from input.  If it equals the expected value, return
   true.  Else print error message to std::cerr and return false*/
inline bool CheckWord(std::istream & input, const std::string & expected) {
        std::string s;
  if (! input.good()) {
    std::cerr << "premature end of input.  (" << expected << ")\n";
    return false;
  }
  input >> s;
  if (s == expected)
    return true;
  std::cerr
    << "format error.  Expected \"" << expected
    << "\", but got \"" << s << "\".\n";
  return false;
}

/**
   Read an integer from input.  If it equals the expected value, return
   true.  Else print error message to std::cerr and return false*/
inline bool CheckInteger(std::istream & input, int i,
    const std::string & errorMessage) {
  if (! input.good()) {
    std::cerr << "premature end of input.  ("
      << errorMessage << ")\n";
    return false;
  }
  int j;
  input >> j;
  if (i == j)
    return true;
  std::cerr
    << "format error.  Expected \"" << i
    << "\", but got \"" << j << "\".  ("
    << errorMessage << ")\n";
  return false;
}


/**
   Read and save comments to a vector of strings.  Will append
   comments to existing comments. */
bool parseComments(
    std::vector< std::string > & returnStrings,
    std::istream & i) {
  static const char comment_character = '#';
  if (! i.good()) return false;
  int c = i.peek();
  while ( i.good() && ( ( c == comment_character ) || ( c == '\n') ) ) {
    std::string s;
    std::getline(i, s);
    returnStrings.push_back(s);
    c = i.peek();
  }
  if (i.fail()) return false;
  return true;
}


std::ostream & serializeComments(
    const std::vector< std::string > & comments,
    std::ostream & o) {
  for (unsigned int i = 0; i < comments.size(); ++i) {
    o << comments[i] << '\n';
  }
  return o;
}

/**
   Populates a matrix from a istream.  Expects positive integers
   number_rows and number_columns to be listed first. Reads elements
   in row-major order.

   Returns false if reading from the istream fails.  Otherwise
   true. */
template < typename TDerived >
inline bool ReadMatrix(
    const Eigen::MatrixBase< TDerived > & m_,
    std::istream & ins)
{
  unsigned int nrows, ncols;
  Eigen::MatrixBase< TDerived > & m
    = const_cast< Eigen::MatrixBase< TDerived > & >(m_);
  if (! ins.good()) return false;
  ins >> nrows >> ncols;
  m.derived().resize(nrows, ncols);
  for (size_t i = 0; i < nrows; ++i)
    for (size_t j = 0; j < ncols; ++j) {
      if (! ins.good()) return false;
      ins >> m(i,j);
    }
  return true;
}

/**
   Populates a Vector from a istream.  Expects positive integers
   number_elements to be listed first. Reads elements in order.

   Returns false if reading from the istream fails.  Otherwise true.
 */
template < typename TDerived >
inline bool ReadVector(
    const Eigen::MatrixBase< TDerived > & v_,
    std::istream & ins)
{
  unsigned int nrows;
  Eigen::MatrixBase< TDerived > & v
    = const_cast< Eigen::MatrixBase< TDerived > & >(v_);
  if (! ins.good()) return false;
  ins >> nrows;
  v.derived().resize(nrows, 1);
  for (size_t i = 0; i < nrows; ++i) {
    if (! ins.good()) return false;
    ins >> v(i,0);
  }
  return true;
}

/**
   Print a Matrix to output stream, preceded by its dimensions.  Use
   row-major order. */
template < typename TDerived >
static inline std::ostream & PrintMatrix(
    const Eigen::MatrixBase< TDerived > & m,
    std::ostream & o)
{
  o << m.rows() << ' ' << m.cols() << '\n';
  if (m.cols() > 0) {
    for (int i = 0; i < m.rows(); ++i) {
      for (int j = 0; j < (m.cols() - 1); ++j)
        o << m(i, j) << '\t';
      o << m(i, m.cols() - 1) << '\n';
    }
  }
  return o;
}

/**
   Print a Vector to output stream, preceded by its size. */
template < typename TDerived >
static inline std::ostream & PrintVector(
    const Eigen::MatrixBase< TDerived > & v,
    std::ostream & o)
{
  o << v.size() << '\n';
  for (int i = 0; i < v.size(); ++i) {
    o << v(i) << '\n';
  }
  return o;
}


/**
 * A covariance function can be represented as a string. */
const char * GetCovarianceFunctionString(
  GaussianProcessModelEmulator::CovarianceFunction cov)
{
  switch (cov) {
  case GaussianProcessModelEmulator::POWER_EXP_FN:  return "POWER_EXP_FN";
  case GaussianProcessModelEmulator::SQUARE_EXP_FN: return "SQUARE_EXP_FN";
  case GaussianProcessModelEmulator::MATERN_32_FN:  return "MATERN_32_FN";
  case GaussianProcessModelEmulator::MATERN_52_FN:  return "MATERN_52_FN";
  default:
    assert(false);
    return "UNKNOWN";
  }
}
/**
   Read the CovarianceFunction from the command line. */
bool parseCovarianceFunction(
    GaussianProcessModelEmulator::CovarianceFunction & cov,
    std::istream & input) {
  if (! input.good()) return false;
  std::string s;
  input >> s;
  if (s == "POWER_EXP_FN")
    cov = GaussianProcessModelEmulator::POWER_EXP_FN;
  else if (s == "SQUARE_EXP_FN")
    cov = GaussianProcessModelEmulator::SQUARE_EXP_FN;
  else if (s == "MATERN_32_FN")
    cov = GaussianProcessModelEmulator::MATERN_32_FN;
  else if (s == "MATERN_52_FN")
    cov = GaussianProcessModelEmulator::MATERN_52_FN;
  else {
    return false;
  }
  return true;
}

/**
   Read an integer from the input, followed by that number of
   parameters.  Populate the numberParameters vector. */
bool parseParameters(
    std::vector<madai::Parameter> & parameters,
    int & numberParameters,
    std::istream & input) {
  if (! input.good()) return false;
  input >> numberParameters;
  parameters.reserve(numberParameters);
  for(int i = 0; i < numberParameters; ++i) {
    std::string name, priotDistType;
    if (! input.good()) return false;
    input >> name >> priotDistType;
    if (priotDistType == "UNIFORM") {
      double min, max;
      input >> min >> max;
      madai::UniformDistribution priorDist;
      priorDist.SetMinimum(min);
      priorDist.SetMaximum(max);
      parameters.push_back(madai::Parameter(name, priorDist));
    } else if (priotDistType == "GAUSSIAN") {
      double mean, std;
      input >> mean >> std;
      madai::GaussianDistribution priorDist;
      priorDist.SetMean(mean);
      priorDist.SetStandardDeviation(std);
      parameters.push_back(madai::Parameter(name, priorDist));
    } else {
      std::cerr << "Expected UNIFORM or GAUSSIAN, but got " <<
        priotDistType <<'\n';
      return false;
    }
  } // end for loop
  return true;
}

std::ostream & serializeParameters(
    const std::vector<madai::Parameter> & parameters,
    std::ostream & o) {
  int numberParameters = parameters.size();
  o << numberParameters << '\n';
  for(int i = 0; i < numberParameters; ++i) {
    o << parameters[i].m_Name << '\t';
    const Distribution * priorDist = parameters[i].GetPriorDistribution();
    const UniformDistribution * uniformPriorDist
      = dynamic_cast<const UniformDistribution *>(priorDist);
    const GaussianDistribution * gaussianPriorDist
      = dynamic_cast<const GaussianDistribution *>(priorDist);
    if (uniformPriorDist != NULL) {
      o << "UNIFORM" << '\t'
        << uniformPriorDist->GetMinimum() << '\t'
        << uniformPriorDist->GetMaximum() << '\n';
    } else if (gaussianPriorDist != NULL) {
      o << "GAUSSIAN" << '\t'
        << gaussianPriorDist->GetMean() << '\t'
        << gaussianPriorDist->GetStandardDeviation() << '\n';
    } else {
      assert(false);
      o << "UNKNOWN_PRIOR_TYPE\t0\t1\n";
    }
  }
  return o;
}


bool parseOutputs(
    std::vector< std::string > & outputNames,
    int & numberOutputs,
    std::istream & input) {
  if (! input.good()) return false;
  input >> numberOutputs;
  outputNames.resize(numberOutputs);
  for(int i = 0; i < numberOutputs; ++i) {
    if (! input.good()) return false;
    input >> outputNames[i];
  }
  return true;
}

std::ostream & serializeStringVector(
    const std::vector< std::string > & strings,
    std::ostream & o) {
  o << strings.size() << '\n';
  for(unsigned int i = 0; i < strings.size(); ++i) {
    o << strings[i] << '\n';
  }
  return o;
}

bool parseInteger( int & x, std::istream & input ) {
  if (! input.good()) return false;
  input >> x;
  return true;
}

bool parseSubmodels(
    GaussianProcessModelEmulator::SingleModel & m,
    int modelIndex,
    std::istream & input) {
  std::string word;
  if (! CheckWord(input, "MODEL")) return false;
  if (! CheckInteger(input, modelIndex, "modelIndex")) return false;
  while (input.good()) {
    if (! input.good()) return false;
    input >> word;
    if (word == "COVARIANCE_FUNCTION") {
      if (!parseCovarianceFunction(m.m_CovarianceFunction,  input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "Z_VALUES") {
      if (! ReadVector(m.m_ZValues, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "REGRESSION_ORDER") {
      if (! parseInteger(m.m_RegressionOrder, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "THETAS") {
      if (! ReadVector(m.m_Thetas, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "END_OF_MODEL") {
      return true;
    } else {
      std::cerr << "Unexected keyword: \"" << word << "\"\n";
      return false;
    }
  }
  return false;
}

std::ostream & serializeSubmodels(
    const GaussianProcessModelEmulator::SingleModel & m,
    int modelIndex,
    std::ostream & o) {
  o << "MODEL " << modelIndex << '\n';
  o << "COVARIANCE_FUNCTION\t"
    << GetCovarianceFunctionString(m.m_CovarianceFunction) << '\n';
  o << "REGRESSION_ORDER\t" << m.m_RegressionOrder << '\n';
  o << "Z_VALUES\n";
  PrintVector(m.m_ZValues, o);
  o << "THETAS\n";
  PrintVector(m.m_Thetas, o);
  o << "END_OF_MODEL\n";
  return o;
}

std::ostream & serializeGaussianProcessModelEmulator(
    const GaussianProcessModelEmulator & gpme,
    std::ostream & o) {

  serializeComments(gpme.m_Comments,o);
  o << "VERSION 1\n";
  o << "PARAMETERS\n";
  serializeParameters(gpme.m_Parameters,o);
  o << "OUTPUTS\n";
  serializeStringVector(gpme.m_OutputNames,o);
  o << "NUMBER_OF_TRAINING_POINTS\t"
    << gpme.m_NumberTrainingPoints << '\n';
  o << "PARAMETER_VALUES\n";
  PrintMatrix(gpme.m_ParameterValues, o);
  o << "OUTPUT_VALUES\n";
  PrintMatrix(gpme.m_OutputValues, o);
  o << "OUTPUT_MEANS\n";
  PrintVector(gpme.m_OutputMeans, o);
  o << "OUTPUT_STANDARD_DEVIATIONS\n";
  PrintVector(gpme.m_OutputStandardDeviations, o);
  o << "OUTPUT_PCA_EIGENVALUES\n";
  PrintVector(gpme.m_PCAEigenvalues, o);
  o << "OUTPUT_PCA_EIGENVECTORS\n";
  PrintMatrix(gpme.m_PCAEigenvectors, o);
  o << "SUBMODELS\t"
    << gpme.m_NumberPCAOutputs << "\n";
  for (int i = 0; i < gpme.m_NumberPCAOutputs; ++i) {
    serializeSubmodels(gpme.m_PCADecomposedModels[i],i,o);
  }
  o << "END_OF_FILE\n";
  return o;
}

bool parseGaussianProcessModelEmulator(
    GaussianProcessModelEmulator & gpme,
    std::istream & input) {
  parseComments(gpme.m_Comments,input);
  std::string word;
  if (! CheckWord(input, "VERSION")) return false;
  if (! CheckInteger(input, 1, "versn")) return false;
  while (input.good()) {
    if (! input.good()) return false;
    input >> word;
    if (word == "PARAMETERS") {
      if (! parseParameters(gpme.m_Parameters,
                            gpme.m_NumberParameters,
                            input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "OUTPUTS") {
      if (! parseOutputs(gpme.m_OutputNames,
                         gpme.m_NumberOutputs,
                         input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "NUMBER_OF_TRAINING_POINTS") {
      if (! parseInteger(gpme.m_NumberTrainingPoints, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "PARAMETER_VALUES") {
      if (! ReadMatrix(gpme.m_ParameterValues, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "OUTPUT_VALUES") {
      if (! ReadMatrix(gpme.m_OutputValues, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "OUTPUT_MEANS") {
      if (! ReadVector(gpme.m_OutputMeans, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "OUTPUT_STANDARD_DEVIATIONS") {
      if (! ReadVector(gpme.m_OutputStandardDeviations, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "OUTPUT_PCA_EIGENVALUES") {
      if (! ReadVector(gpme.m_PCAEigenvalues, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "OUTPUT_PCA_EIGENVECTORS") {
      if (! ReadMatrix(gpme.m_PCAEigenvectors, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
    } else if (word == "SUBMODELS") {
      if (! parseInteger(gpme.m_NumberPCAOutputs, input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
      }
      gpme.m_PCADecomposedModels.resize(gpme.m_NumberPCAOutputs);
      for (int i = 0; i < gpme.m_NumberPCAOutputs; ++i) {
        if (! parseSubmodels(gpme.m_PCADecomposedModels[i],i,input)) {
        std::cerr << "parse error\n"; // \todo error message
        return false;
        }
        gpme.m_PCADecomposedModels[i].m_Parent = &gpme;
      }
    } else if (word == "END_OF_FILE") {
      return true;
    } else {
      std::cerr << "Unexected keyword: \"" << word << "\"\n";
      return false;
    }
  }
  return false;
}

inline int NumberRegressionFunctions(
    int regressionOrder,
    int numberParameters) {
  return 1 + (regressionOrder * numberParameters);
}

inline int NumberThetas(
    GaussianProcessModelEmulator::CovarianceFunction cf,
    int numberParameters) {
  switch(cf) {
  case GaussianProcessModelEmulator::SQUARE_EXP_FN:
    return numberParameters + 2;
  case GaussianProcessModelEmulator::POWER_EXP_FN:
    return numberParameters + 3;
  case GaussianProcessModelEmulator::MATERN_32_FN:
    return 3;
  case GaussianProcessModelEmulator::MATERN_52_FN:
    return 3;
  case GaussianProcessModelEmulator::UNKNOWN_FN:
    //fall through
  default:
    return -1;
  }
}

const char * stat(GaussianProcessModelEmulator::StatusType s) {
  switch(s) {
  case GaussianProcessModelEmulator::READY        : return "READY";
  case GaussianProcessModelEmulator::UNCACHED     : return "UNCACHED";
  case GaussianProcessModelEmulator::UNTRAINED    : return "UNTRAINED";
  case GaussianProcessModelEmulator::UNINITIALIZED: return "UNINITIALIZED";
  case GaussianProcessModelEmulator::ERROR        :
  default                                         : return "ERROR";
  }
}

} // anonymous namespace

namespace madai {

double GaussianProcessModelEmulator::SingleModel::CovarianceCalc(
    const Eigen::VectorXd & v1, const Eigen::VectorXd & v2) const
{
  int p = m_Parent->m_NumberParameters;
  int numberThetas = m_Thetas.size();
  assert(numberThetas > 0);
  switch(m_CovarianceFunction) {
  case POWER_EXP_FN:
    {
      assert(numberThetas == (p + 3));
      assert ((m_Thetas(2) > 0.0) && (m_Thetas(2) <= 2.0));
      // theta(0) == ln(amplitude)
      // theta(1) == nugget
      // theta(2) == power
      double covariance = 0.0;
      double exponent = 0.0;
      for(int i = 0; i < p; i++){
        double d = std::abs(v1(i) - v2(i));
        exponent += (-0.5) * std::pow(
            d / std::exp(std::abs(m_Thetas(i + 3))), m_Thetas(2));
      }
      covariance = exp(m_Thetas(0) + exponent);
      if (exponent < 1e-5)
        covariance += exp(m_Thetas(1));
      return covariance;
    }
  case SQUARE_EXP_FN:
    {
      assert(numberThetas == (p + 2));
      int truecount  = 0;
      double covariance = 0.0;
      double exponent = 0.0;
      // theta(0) == ln(amplitude)
      // theta(1) == nugget
      // theta(2+i) == scale[i]
      for(int i = 0; i < p; i++){
        double d = std::abs(v1(i) - v2(i));
        exponent += (-0.5) * std::pow(d / std::exp(m_Thetas(i + 2)), 2);
        if (d < 1e-10)
          truecount++;
      }
      covariance = exp(m_Thetas(0)) * exp(exponent);
      if(truecount == p) {
        covariance += exp(m_Thetas(1));
      }
      return covariance;
    }
  case MATERN_32_FN:
    {
      assert(numberThetas == 3);
      static const double ROOT3 = 1.7320508075688772;
      double distance = 0.0;
      double covariance;
      int truecount = 0;
      // map the m_Thetas onto some local variables so the formula
      // is more transparent
      double amp = m_Thetas(0);
      double nugget = m_Thetas(1);
      double rho = std::exp(m_Thetas(2));
      // calculate the euclidean distance between the two points;
      for(int i = 0; i < p; i++) {
        double d = std::pow(std::abs(v1(i) - v2(i)), 2);
        distance += d;
        if (d < 1e-16)
          ++truecount;
        // this is currently the distance squared
      }
      distance = std::sqrt(distance); // reduce back to the right dimensions
      if (distance > 0.0)
        covariance = amp * (1 + ROOT3 * (distance / rho))
          * std::exp(-ROOT3 * (distance / rho));
      else
        covariance = amp;
      if (truecount == p)
        covariance += nugget;
      return covariance;
    }
  case MATERN_52_FN:
    {
      assert(numberThetas == 3);
      int truecount = 0;
      double distance = 0.0;
      double covariance = 0.0;
      // map the m_Thetas onto some local variables so the formula
      // is more transparent
      double amp = m_Thetas(0);
      double nugget = m_Thetas(1);
      double rho = std::exp(m_Thetas(2));
      static const double ROOT5 = 2.23606797749979;
      double d_over_r = 0.0;
      // calculate the euclidean distance between the two points;
      for (int i = 0; i < p; i++){
        double d = std::abs(v1(i) - v2(i));
        // this is currently the distance squared
        distance += std::pow(d, 2.0);
        if (d < 1e-16)
          ++truecount;
      }
      // reduce back to the right dimensions
      distance = std::sqrt(distance);
      d_over_r = distance / rho;
      if (distance > 0.0){
        covariance
          = amp * (1 + ROOT5 * d_over_r + (5.0 / 3.0) * d_over_r * d_over_r)
          * std:: exp(- ROOT5 * d_over_r);
      } else {
        covariance = amp;
      }
      // this means we're on a diagonal term, golly but i write bad code :(
      if (truecount == p){
        covariance += nugget;
      }
      return covariance;
    }
  default:
    assert(false);
  }
  return 0.0;
}


bool GaussianProcessModelEmulator::Load(std::istream & input)
{
  m_Status = UNINITIALIZED;
  if (! parseGaussianProcessModelEmulator(*this, input)) {
    std::cerr << "FIXME need error message\n";
    return false;
  }
  // We are finished reading the input file.
  if (this->CheckStatus() != GaussianProcessModelEmulator::UNCACHED) {
    std::cerr << "FIXME status error\t" << stat(this->CheckStatus())
    << '\n';
    return false;
  }
  if (! this->MakeCache()) {
    std::cerr << "FIXME need error message\n";
    return false;
  }
  return true;
}

GaussianProcessModelEmulator::StatusType
GaussianProcessModelEmulator::CheckStatus() {
  m_Status = UNINITIALIZED;
  if (m_NumberTrainingPoints < 1) {
    return m_Status;
  }
  if (m_NumberParameters < 1) {
    return m_Status;
  }
  if (m_NumberOutputs < 1) {
    return m_Status;
  }
  if (static_cast<int>(m_Parameters.size()) != m_NumberParameters) {
    return m_Status;
  }
  if (static_cast<int>(m_OutputNames.size()) != m_NumberOutputs) {
    return m_Status;
  }
  if (m_ParameterValues.rows() != m_NumberTrainingPoints) {
    return m_Status;
  }
  if (m_OutputValues.rows() != m_NumberTrainingPoints) {
    return m_Status;
  }
  if (m_ParameterValues.cols() != m_NumberParameters) {
    return m_Status;
  }
  if (m_OutputValues.cols() != m_NumberOutputs) {
    return m_Status;
  }
  m_Status = UNTRAINED;
  if(m_NumberPCAOutputs < 1)
    return m_Status;
  // std::cerr << 1 << '\n';
  if(m_OutputMeans.size() != m_NumberOutputs)
    return m_Status;
  // std::cerr << 2 << '\n';
  if(m_OutputStandardDeviations.size() != m_NumberOutputs)
    return m_Status;
  // std::cerr << 3 << '\n';
  if(m_PCAEigenvalues.size() != m_NumberPCAOutputs)
    return m_Status;
  // std::cerr << 4 << '\n';
  if(m_PCAEigenvectors.rows() != m_NumberOutputs)
    return m_Status;
  // std::cerr << 5 << '\n';
  if(m_PCAEigenvectors.cols() != m_NumberPCAOutputs)
    return m_Status;
  // std::cerr << 6 << '\n';
  if (static_cast<int>(m_PCADecomposedModels.size()) != m_NumberPCAOutputs)
    return m_Status;
  // std::cerr << 7 << '\n';
  for (int i = 0; i < m_NumberPCAOutputs; ++i) {
    SingleModel & m = m_PCADecomposedModels[i];
    if (m.m_Parent == NULL)
      return m_Status;
    // std::cerr << 8 << '\n';
    if (m.m_CovarianceFunction == UNKNOWN_FN)
      return m_Status;
    // std::cerr << 9 << '\n';
    if (m.m_RegressionOrder < 0)
      return m_Status;
    // std::cerr << 10 << '\n';
    if (m.m_ZValues.size() != m_NumberTrainingPoints)
      return m_Status;
    // std::cerr << 11 << '\n';
    if (m.m_Thetas.size() != NumberThetas(m.m_CovarianceFunction,
                                          m_NumberParameters))
      return m_Status;
    // std::cerr << 12 << '\n';
  }
  m_Status = UNCACHED;
  for (int i = 0; i < m_NumberPCAOutputs; ++i) {
    SingleModel & m = m_PCADecomposedModels[i];
    if (m.m_CInverse.rows() != m_NumberTrainingPoints)
      return m_Status;
    if (m.m_CInverse.cols() != m_NumberTrainingPoints)
      return m_Status;
    int F = NumberRegressionFunctions(m.m_RegressionOrder,m_NumberParameters);
    if (m.m_BetaVector.size() != F)
      return m_Status;
    if (m.m_HMatrix.rows() != m_NumberTrainingPoints)
      return m_Status;
    if (m.m_HMatrix.cols() != F)
      return m_Status;
    if (m.m_CInverseZ.rows() != m_NumberTrainingPoints)
      return m_Status;
    if (m.m_CInverseZ.cols() != m_NumberTrainingPoints)
      return m_Status;
    if (m.m_CInverseHtrans.rows() != F)
      return m_Status;
    if (m.m_CInverseHtrans.cols() != m_NumberTrainingPoints)
      return m_Status;
    if (m.m_IHTCIH.rows() != F)
      return m_Status;
    if (m.m_IHTCIH.cols() != F)
      return m_Status;
  }
  m_Status = READY;
  return m_Status;
}


/**
   Set default values to uninitialized values. */
GaussianProcessModelEmulator::SingleModel::SingleModel() :
  m_Parent(NULL),
  m_CovarianceFunction(UNKNOWN_FN),
  m_RegressionOrder(-1)
{ }


/**
   Once Load(), Train(), or BasicTraining() finishes, calculate and
   cache some data to make calling GetEmulatorOutputsAndCovariance()
   faster. */
bool GaussianProcessModelEmulator::MakeCache() {
  if ((m_Status != READY) && (m_Status != UNCACHED))
    return false;
  assert(m_NumberPCAOutputs == static_cast<int>(m_PCADecomposedModels.size()));
  for (int i = 0; i < m_NumberPCAOutputs; ++i) {
    if (! m_PCADecomposedModels[i].MakeCache())
      return false;
  }
  m_Status = READY;
  return true;
}
bool GaussianProcessModelEmulator::SingleModel::MakeCache() {
  int N = m_Parent->m_NumberTrainingPoints;
  int p = m_Parent->m_NumberParameters;
  int F = NumberRegressionFunctions(m_RegressionOrder, p);
  Eigen::MatrixXd & X = m_Parent->m_ParameterValues;

  m_CInverse.resize(N, N);
  m_BetaVector.resize(F);
  m_HMatrix.resize(N, F);

  Eigen::MatrixXd c_matrix(N, N);
  // c_matrix is the covariance matrix of the design with itself.
  for (int j = 0; j < N; ++j) {
    Eigen::VectorXd rowj = X.row(j);
    for (int k = j; k < N; ++k) {
      Eigen::VectorXd rowk = X.row(k);
      c_matrix(j,k) = this->CovarianceCalc(rowj, rowk);
      if (j != k)
        c_matrix(k,j) = c_matrix(j,k);
    }
  }

  // we have to use this inverted matrix so often, we may as well go
  // ahead and fully invert it.
  Eigen::LDLT< Eigen::MatrixXd > Ccholesky(c_matrix);
  // cInverse = C^{-1}
  m_CInverse = Ccholesky.solve(Eigen::MatrixXd::Identity(N,N));
  //double determinant = std::pow(Ccholesky.vectorD().prod(),2);

  assert(F == 1 + ((m_RegressionOrder) * p));
  switch(m_RegressionOrder) {
  case 3:
    m_HMatrix.block(0,p+p+1,N,p) = X.cwiseProduct(X).cwiseProduct(X);
    // fall through
  case 2:
    m_HMatrix.block(0,p+1,N,p) = X.cwiseProduct(X);
    // fall through
  case 1:
    m_HMatrix.block(0,1,N,p) = X;
    // fall through
  case 0:
    m_HMatrix.block(0,0,N,1) = Eigen::MatrixXd::Constant(N,1, 1.0);
    break;
  default:
    assert(false);
  }

  // HTransCInverse =
  Eigen::MatrixXd HTransCInverse = m_HMatrix.transpose() * m_CInverse;
  // (H^T C^{-1} H)-1
  Eigen::LDLT< Eigen::MatrixXd > HCHcholesky(HTransCInverse * m_HMatrix);
  m_BetaVector = HCHcholesky.solve(HTransCInverse * m_ZValues);

  m_CInverseZ = m_CInverse * m_ZValues;
  // m_CInverseHtrans = (m_CInverse * m_HMatrix).transpose();
  m_CInverseHtrans = m_HMatrix.transpose() * m_CInverse.transpose();

  Eigen::LDLT< Eigen::MatrixXd > HTCIHcholesky(m_HMatrix.transpose()
    * m_CInverse * m_HMatrix);
  m_IHTCIH = HTCIHcholesky.solve(Eigen::MatrixXd::Identity(F,F));
  return true;
}

/**
   Default to uninitialized state. */
GaussianProcessModelEmulator::GaussianProcessModelEmulator() :
  m_Status(UNINITIALIZED),
  m_NumberParameters(0),
  m_NumberOutputs(0),
  m_NumberTrainingPoints(0),
  m_NumberPCAOutputs(0)
{ }

/**
   This takes an empty GPEM and loads training data */
bool GaussianProcessModelEmulator::LoadTrainingData(std::istream & input) {
  m_Status = UNINITIALIZED;
  if (! parseGaussianProcessModelEmulator(*this, input))
    return false;
  m_NumberPCAOutputs = 0;
  this->CheckStatus();
  return (m_Status == UNTRAINED);
}


/**
   This takes an GPEM and trains it. \returns true on sucess. */
bool GaussianProcessModelEmulator::SingleModel::Train(
    GaussianProcessModelEmulator::CovarianceFunction covarianceFunction,
    int regressionOrder)
{
  if (regressionOrder < 0) {
    /* \todo error message to stderr */
    return false;
  }
  if (regressionOrder > 3) {
    /* \todo error message to stderr */
    return false;
  }
  m_CovarianceFunction = covarianceFunction;
  m_RegressionOrder = regressionOrder;
  int numberThetas
    = NumberThetas(covarianceFunction, m_Parent->m_NumberParameters);
  m_Thetas.resize(numberThetas);
  std::cerr << "Sorry, but this function is not yet implemented.\n";
  // FIXME
  return false;
}


/**
   This takes an GPEM and trains it. \returns true on sucess. */
bool GaussianProcessModelEmulator::Train(
    GaussianProcessModelEmulator::CovarianceFunction covarianceFunction,
    int regressionOrder,
    double fractionResolvingPower)
{
  if (this->CheckStatus() == UNINITIALIZED)
    return false;
  m_Status = UNTRAINED;
  if (! this->PrincipalComponentDecompose(fractionResolvingPower))
    return false;
  for (int i = 0; i < m_NumberPCAOutputs; ++i) {
    if (! m_PCADecomposedModels[i].Train(covarianceFunction,regressionOrder))
      return false;
  }
  return true;
}

/**
   This takes an GPEM and trains it. \returns true on sucess. */
bool GaussianProcessModelEmulator::BasicTraining(
    double fractionResolvingPower,
    CovarianceFunction covarianceFunction,
    int regressionOrder,
    double defaultNugget,
    double amplitude,
    double scale)
{
  if (this->CheckStatus() == UNINITIALIZED)
    return false;
  m_Status = UNTRAINED;
  if (! this->PrincipalComponentDecompose(fractionResolvingPower))
    return false;
  for (int i = 0; i < m_NumberPCAOutputs; ++i) {
    if (! m_PCADecomposedModels[i].BasicTraining(covarianceFunction,
            regressionOrder, defaultNugget, amplitude, scale))
      return false;
  }
  m_Status = UNCACHED;
  return true;
}
/**
   Sets default values for all of the hyperparameters. \returns
   true on success. */
bool GaussianProcessModelEmulator::SingleModel::BasicTraining(
    CovarianceFunction covarianceFunction,
    int regressionOrder,
    double defaultNugget,
    double amplitude,
    double scale) {
  m_CovarianceFunction = covarianceFunction;
  m_RegressionOrder = regressionOrder;
  int p = m_Parent->m_NumberParameters;
  m_Thetas.resize( NumberThetas(m_CovarianceFunction, p));
  scale = std::abs(scale);
  switch(m_CovarianceFunction) {
  case GaussianProcessModelEmulator::SQUARE_EXP_FN:
    m_Thetas.resize(2 + p);
    m_Thetas(0) = amplitude;
    m_Thetas(1) = defaultNugget;
    for (int j = 0; j < p; ++j) {
      madai::Parameter & param = m_Parent->m_Parameters[j];
      madai::Distribution * priordist = param.m_PriorDistribution;
      m_Thetas(2+j) = scale * std::abs(priordist->GetPercentile(0.75)
                                     - priordist->GetPercentile(0.25));
    }
    break;
  case GaussianProcessModelEmulator::POWER_EXP_FN:
    m_Thetas.resize(3 + p);
    m_Thetas(0) = amplitude;
    m_Thetas(1) = defaultNugget;
    m_Thetas(2) = 2.0;
    for (int j = 0; j < p; ++j) {
      madai::Parameter & param = m_Parent->m_Parameters[j];
      madai::Distribution * priordist = param.m_PriorDistribution;
      m_Thetas(3+j) = scale * std::abs(priordist->GetPercentile(0.75)
                                     - priordist->GetPercentile(0.25));
    }
    break;
  case GaussianProcessModelEmulator::MATERN_32_FN:
    // fall through
  case GaussianProcessModelEmulator::MATERN_52_FN:
    m_Thetas.resize(3);
    m_Thetas(0) = amplitude;
    m_Thetas(1) = defaultNugget;
    {
      double min = std::numeric_limits< double >::max();
      for (int j = 0; j < p; ++j) {
        madai::Parameter & param = m_Parent->m_Parameters[j];
        madai::Distribution * priordist = param.m_PriorDistribution;
        double d = std::abs(priordist->GetPercentile(0.75)
                          - priordist->GetPercentile(0.25));
        if (d < min)
          min = d;
      }
      m_Thetas(2) = min * scale;
    }
    break;
  default:
    std::cerr << "Unknown covariance function.\n";
    return false;
  }
  return true;
}

bool GaussianProcessModelEmulator::PrincipalComponentDecompose(
    double fractionResolvingPower)
{
  if (fractionResolvingPower <= 0.0) {
    /* \todo error message to stderr */
    return false;
  }
  if (fractionResolvingPower > 1.0) {
    /* \todo error message to stderr */
    return false;
  }
  int t = m_NumberOutputs;
  int N = m_NumberTrainingPoints;

  // FIND PCA DECOMPOSIRION OF m_OutputValues - m_OutputMeans
  m_OutputMeans = m_OutputValues.colwise().mean();
  Eigen::MatrixXd Y_minus_means
    = m_OutputValues.rowwise() - (m_OutputValues.colwise().mean());

  //original training set (rows:numberTrainingPoints) (cols:numberOutputs) */
  //Eigen::VectorXd m_OutputStandardDeviation;
  m_OutputStandardDeviations.resize(t);

  for (int outputIndex = 0; outputIndex < t; ++outputIndex)
    m_OutputStandardDeviations(outputIndex)
      = std::sqrt(Y_minus_means.col(outputIndex).squaredNorm());

  Eigen::MatrixXd Y_standardized(N,t);
  for (int outputIndex = 0; outputIndex < t; ++outputIndex) {
    double one_over_std_dev
      = 1.0 / m_OutputStandardDeviations(outputIndex);
    for (int pointIndex = 0; pointIndex < N; ++pointIndex) {
      Y_standardized(pointIndex, outputIndex)
        = one_over_std_dev * Y_minus_means(pointIndex, outputIndex);
    }
  }

  Eigen::MatrixXd Ycov
    = (1.0 / N ) * Y_standardized.transpose() * Y_standardized;

  Eigen::SelfAdjointEigenSolver< Eigen::MatrixXd > eigenSolver(Ycov);


  double resolving_power = 1.0;
  for (int i = t-1; i >= 0; --i)
    resolving_power *= std::sqrt(1.0 + eigenSolver.eigenvalues()(i));
  double target_resolving_power = resolving_power * fractionResolvingPower;

  resolving_power = 1.0;
  for (int i = t-1; i >= 0; --i) {
    resolving_power *= std::sqrt(1.0 + eigenSolver.eigenvalues()(i));
    if (resolving_power >= target_resolving_power) {
      m_NumberPCAOutputs = t - i;
      break;
    }
  }

  int r = m_NumberPCAOutputs;
  assert((r > 0) && (r <= t));
  m_PCAEigenvalues = eigenSolver.eigenvalues().tail(r);
  m_PCAEigenvectors = eigenSolver.eigenvectors().rightCols(r);

  Eigen::MatrixXd zMatrix = Y_standardized * m_PCAEigenvectors;
  m_PCADecomposedModels.resize(r);
  for (int i = 0; i < r; ++i) {
    SingleModel & m = m_PCADecomposedModels[i];
    m.m_Parent = this;
    m.m_ZValues = zMatrix.col(i);
  }
  return true;
}


/**
   Execute the model at an input point x.  Save a lot of time by not
   calculating the error. */
bool GaussianProcessModelEmulator::SingleModel::GetEmulatorOutputs (
    const std::vector< double > & x,
    double & mean) const {
  assert(m_RegressionOrder >= 0);
  Eigen::Map<const Eigen::VectorXd> point(&(x[0]),x.size());
  int N = m_Parent->m_NumberTrainingPoints;
  int p = m_Parent->m_NumberParameters;
  assert(p > 0);
  int F = 1 + (m_RegressionOrder * p);
  Eigen::MatrixXd & X = m_Parent->m_ParameterValues;
  Eigen::VectorXd kplus(N);
  // kplus is C(x,D)
  for (int j = 0; j < N; ++j) {
    Eigen::VectorXd xrow = X.row(j);
    double cov = this->CovarianceCalc(xrow, point);
    if(cov < 1e-10)
      cov = 0.0;
    kplus(j) = cov;
  }
  Eigen::VectorXd h_vector(F);
  h_vector(0) = 1.0;
  if (m_RegressionOrder > 0)
    h_vector.segment(1,p) = point;
  for (int i = 2; i < m_RegressionOrder; ++i) {
    h_vector.segment(1+(i*p),p)
      = h_vector.segment(1+((i-1)*p),p).cwiseProduct(point);
  }
  double emulated_mean = kplus.dot(m_CInverseZ);
  // emulated_mean is the mean without the regression applied.
  double regression_cpt = h_vector.dot(m_BetaVector);
  double residual_cpt = m_BetaVector.dot(m_CInverseHtrans * kplus);
  mean = regression_cpt + emulated_mean - residual_cpt;
  return true;
}
/**
   Execute the model at an input point x.  Save a lot of time by not
   calculating the covaraince error. */
bool GaussianProcessModelEmulator::GetEmulatorOutputs (
    const std::vector< double > & x,
    std::vector< double > & y) const
{
  Eigen::VectorXd mean_pca(m_NumberPCAOutputs);
  for (int i = 0; i < m_NumberPCAOutputs; ++i) {
    double d;
    if(! m_PCADecomposedModels[i].GetEmulatorOutputs(x, d))
      return false;
    mean_pca(i) = d;
  }
  y.resize(m_NumberOutputs);
  Eigen::Map< Eigen::VectorXd > mean(&(y[0]),m_NumberOutputs);
  mean = m_OutputMeans +
    m_OutputStandardDeviations.cwiseProduct(m_PCAEigenvectors * mean_pca);
  return true;
}

/**
   Execute the model at an input point x.  Save a lot of time by not
   calculating the error. */
bool GaussianProcessModelEmulator::SingleModel
::GetEmulatorOutputsAndCovariance (
    const std::vector< double > & x,
    double & mean,
    double & variance) const {
  assert(m_RegressionOrder >= 0);
  Eigen::Map<const Eigen::VectorXd> point(&(x[0]),x.size());
  int N = m_Parent->m_NumberTrainingPoints;
  int p = m_Parent->m_NumberParameters;
  assert(p > 0);
  int F = 1 + (m_RegressionOrder * p);
  Eigen::MatrixXd & X = m_Parent->m_ParameterValues;
  Eigen::VectorXd kplus(N);
  // kplus is C(x,D)
  for (int j = 0; j < N; ++j) {
    Eigen::VectorXd xrow = X.row(j);
    double cov = this->CovarianceCalc(xrow, point);
    if(cov < 1e-10)
      cov = 0.0;
    kplus(j) = cov;
  }
  Eigen::VectorXd h_vector(F);
  h_vector(0) = 1.0;
  if (m_RegressionOrder > 0)
    h_vector.segment(1,p) = point;
  for (int i = 2; i < m_RegressionOrder; ++i) {
    h_vector.segment(1+(i*p),p)
      = h_vector.segment(1+((i-1)*p),p).cwiseProduct(point);
  }
  double emulated_mean = kplus.dot(m_CInverseZ);
  // emulated_mean is the mean without the regression applied.
  double regression_cpt = h_vector.dot(m_BetaVector);
  double residual_cpt = m_BetaVector.dot(m_CInverseHtrans * kplus);
  mean = regression_cpt + emulated_mean - residual_cpt;

  double kappa = this->CovarianceCalc(point, point);
  Eigen::VectorXd result_nreg = h_vector - (m_CInverseHtrans * kplus);
  regression_cpt = result_nreg.dot(m_IHTCIH * result_nreg);
  double emulated_variance = kplus.dot(m_CInverse * kplus);
  variance = (kappa - emulated_variance + regression_cpt);
  return true;
}

/**
         Execute the model at an input point x.
         The covariance returned will be a flattened matrix */
bool GaussianProcessModelEmulator::GetEmulatorOutputsAndCovariance (
    const std::vector< double > & x,
    std::vector< double > & y,
    std::vector< double > & ycov) const {
  if (m_Status != READY)
    return false;

  Eigen::Map<const Eigen::VectorXd> point(&(x[0]),x.size());
  int t = m_NumberOutputs;
  Eigen::VectorXd mean_pca(m_NumberPCAOutputs);
  Eigen::VectorXd var_pca(m_NumberPCAOutputs);
  for (int i = 0; i < m_NumberPCAOutputs; ++i) {
    double smean, variance;
    if (! m_PCADecomposedModels[i].GetEmulatorOutputsAndCovariance(
            x, smean, variance))
      return false;
    mean_pca(i) = smean;
    var_pca(i) = variance;
  } // end-for(i < (m_NumberPCAOutputs))
  y.resize(t);
  ycov.resize(t * t);
  Eigen::Map< Eigen::VectorXd > mean(&(y[0]), t);
  Eigen::Map< Eigen::MatrixXd > covariance(&(ycov[0]), t, t);
  mean = m_OutputMeans +
    m_OutputStandardDeviations.cwiseProduct(m_PCAEigenvectors * mean_pca);

  var_pca = m_PCAEigenvalues.cwiseProduct(var_pca);
  // help!
  // Eigen::VectorXd variances
  //   = m_OutputStandardDeviations.cwiseProduct(m_OutputStandardDeviations);

  Eigen::MatrixXd variances
    = m_OutputStandardDeviations * m_OutputStandardDeviations.transpose();
  covariance
    = variances.cwiseProduct(
        m_PCAEigenvectors * var_pca.asDiagonal() *
        m_PCAEigenvectors.transpose());

  return true;
}


bool GaussianProcessModelEmulator::Write(std::ostream & o) const {
  o.precision(17);
  serializeGaussianProcessModelEmulator(*this, o);
  return true;
}
} // namespace madai
