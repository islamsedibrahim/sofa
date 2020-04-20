/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 RC 1        *
*                (c) 2006-2020 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Modules                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#ifndef SOFA_COMPONENT_INTERACTIONFORCEFIELD_POLYNOMIAL_SPRINGS_FORCEFIELD_INL
#define SOFA_COMPONENT_INTERACTIONFORCEFIELD_POLYNOMIAL_SPRINGS_FORCEFIELD_INL


#include <sofa/core/behavior/ForceField.inl>
#include "PolynomialSpringsForceField.h"
#include <sofa/core/visual/VisualParams.h>
#include <sofa/helper/system/config.h>
#include <sofa/defaulttype/VecTypes.h>
#include <sofa/defaulttype/RigidTypes.h>
#include <sofa/helper/gl/template.h>
#include <sofa/simulation/AnimateBeginEvent.h>
#include <assert.h>
#include <math.h>
#include <iostream>
#include <fstream>
#include <sofa/helper/AdvancedTimer.h>


namespace sofa
{

namespace component
{

namespace interactionforcefield
{


template<class DataTypes>
PolynomialSpringsForceField<DataTypes>::PolynomialSpringsForceField()
    : PolynomialSpringsForceField(nullptr, nullptr)
{
}


template<class DataTypes>
PolynomialSpringsForceField<DataTypes>::PolynomialSpringsForceField(MechanicalState* mstate1, MechanicalState* mstate2)
    : Inherit(mstate1, mstate2)
    , d_firstObjectPoints(initData(&d_firstObjectPoints, "firstObjectPoints", "points related to the first object"))
    , d_secondObjectPoints(initData(&d_secondObjectPoints, "secondObjectPoints", "points related to the second object"))
    , d_polynomialStiffness(initData(&d_polynomialStiffness, "polynomialStiffness", "coefficients for all spring polynomials"))
    , d_polynomialDegree(initData(&d_polynomialDegree, "polynomialDegree", "vector of values that show polynomials degrees"))
    , d_computeZeroLength(initData(&d_computeZeroLength, 1, "computeZeroLength", "flag to compute initial length for springs"))
    , d_zeroLength(initData(&d_zeroLength, "zeroLength", "initial length for springs"))
    , d_recomputeIndices(initData(&d_recomputeIndices, false, "recompute_indices", "Recompute indices (should be false for BBOX)"))
    , d_compressible(initData(&d_compressible, false, "compressible", "Indicates if object compresses without reactio force"))
    , d_drawMode(initData(&d_drawMode, 0, "drawMode", "The way springs will be drawn:\n- 0: Line\n- 1:Cylinder\n- 2: Arrow"))
    , d_showArrowSize(initData(&d_showArrowSize, 0.01f, "showArrowSize","size of the axis"))
    , d_springColor(initData(&d_springColor, defaulttype::RGBAColor(0.0f, 1.0f, 0.0f, 1.0f), "springColor", "spring color"))
    , d_showIndicesScale(initData(&d_showIndicesScale, (float)0.02, "showIndicesScale", "Scale for indices display. (default=0.02)"))
    , Dimension(Coord::total_size)
{
}


template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::bwdInit()
{
    sofa::helper::ReadAccessor< Data<VecReal> > zeroLength = d_zeroLength;

    this->Inherit::init();

    if (d_polynomialStiffness.getValue().empty())
    {
        msg_info() << "ExtendedRestShapeSpringForceField : No stiffness is defined, assuming equal stiffness on each node, k = 100.0 ";
        VecReal stiffs;
        stiffs.push_back(100.0);
        d_polynomialStiffness.setValue(stiffs);
    }

    recomputeIndices();

    // recreate derivatives matrices
    m_differential.resize(m_firstObjectIndices.size());

    m_springLength.resize(m_firstObjectIndices.size());
    m_strainValue.resize(m_firstObjectIndices.size());
    m_weightedCoordinateDifference.resize(m_firstObjectIndices.size());

    m_initialSpringLength.resize(m_firstObjectIndices.size());
    m_computeSpringsZeroLength.resize(m_firstObjectIndices.size());
    if (d_computeZeroLength.getValue() == 1) {
        for (size_t index = 0; index < m_computeSpringsZeroLength.size(); index++) {
            m_computeSpringsZeroLength[index] = 1;
        }
    } else {
        for (size_t index = 0; index < m_computeSpringsZeroLength.size(); index++) {
            m_computeSpringsZeroLength[index] = 0;
            m_initialSpringLength[index] = (zeroLength.size() > 1) ? zeroLength[index] : zeroLength[0];
        }
    }

    m_strainSign.resize(m_firstObjectIndices.size());

    if (d_polynomialDegree.getValue().empty()) {
        helper::WriteAccessor<Data<helper::vector<unsigned int>>> vPolynomialWriteDegree = d_polynomialDegree;
        vPolynomialWriteDegree.push_back(1);
    }

    // read and fill polynomial parameters
    helper::ReadAccessor<Data<helper::vector<unsigned int>>> vPolynomialDegree = d_polynomialDegree;

    m_polynomialsMap.clear();
    helper::vector<unsigned int> polynomial;
    unsigned int inputIndex = 0;
    for (size_t degreeIndex = 0; degreeIndex < vPolynomialDegree.size(); degreeIndex++) {
        polynomial.clear();
        polynomial.resize(vPolynomialDegree[degreeIndex]);
        for (size_t polynomialIndex = 0; polynomialIndex < vPolynomialDegree[degreeIndex]; polynomialIndex++) {
            polynomial[polynomialIndex] = inputIndex;
            inputIndex++;
        }
        m_polynomialsMap.push_back(polynomial);
    }

    // msg_info() << "Polynomial data: ";
    // for (size_t degreeIndex = 0; degreeIndex < vPolynomialDegree.size(); degreeIndex++) {
    //     for (size_t polynomialIndex = 0; polynomialIndex < vPolynomialDegree[degreeIndex]; polynomialIndex++) {
    //         msg_info() << m_polynomialsMap[degreeIndex][polynomialIndex] << " ";
    //     }
    // }

    this->f_listening.setValue(true);
}


template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::recomputeIndices()
{
    m_firstObjectIndices.clear();
    m_secondObjectIndices.clear();

    for (unsigned int i = 0; i < d_firstObjectPoints.getValue().size(); i++)
        m_firstObjectIndices.push_back(d_firstObjectPoints.getValue()[i]);

    for (unsigned int i = 0; i < d_secondObjectPoints.getValue().size(); i++)
        m_secondObjectIndices.push_back(d_secondObjectPoints.getValue()[i]);

    if (m_firstObjectIndices.size() == 0)
    {
        // msg_info() << "default case: points = all points ";
        for (unsigned int i = 0; i < (unsigned)this->mstate1->getSize(); i++) {
            m_firstObjectIndices.push_back(i);
        }
    }

    if (m_secondObjectIndices.size()==0)
    {
        // msg_info() << "default case: points = all points ";
        for (unsigned int i = 0; i < (unsigned)this->mstate2->getSize(); i++) {
            m_secondObjectIndices.push_back(i);
        }
    }

    if (m_firstObjectIndices.size() > m_secondObjectIndices.size())
    {
        msg_error() << "Error : the dimention of the source and the targeted points are different ";
        m_firstObjectIndices.clear();
        m_secondObjectIndices.clear();
    }
}



template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::addForce(const core::MechanicalParams* mparams, DataVecDeriv& data_f1, DataVecDeriv& data_f2,
                                                      const DataVecCoord& data_p1, const DataVecCoord& data_p2,
                                                      const DataVecDeriv& data_v1, const DataVecDeriv& data_v2)
{
    SOFA_UNUSED(mparams);
    SOFA_UNUSED(data_v1);
    SOFA_UNUSED(data_v2);

    VecDeriv&       f1 = *data_f1.beginEdit();
    const VecCoord& p1 =  data_p1.getValue();
    //const VecDeriv& v1 =  data_v1.getValue();
    VecDeriv&       f2 = *data_f2.beginEdit();
    const VecCoord& p2 =  data_p2.getValue();
    //const VecDeriv& v2 =  data_v2.getValue();

    f1.resize(p1.size());
    f2.resize(p2.size());

    if (d_recomputeIndices.getValue())
    {
        recomputeIndices();
    }

    Real compressionValue = d_compressible.getValue() ? -1.0 : 0.0;

    if ( d_polynomialDegree.getValue().size() != m_firstObjectIndices.size() )
    {
        // msg_warning() << "WARNING : stiffness is not defined on each point, first stiffness is used";

        for (unsigned int i = 0; i < m_firstObjectIndices.size(); i++)
        {
            const unsigned int firstIndex = m_firstObjectIndices[i];
            const unsigned int secondIndex = m_secondObjectIndices[i];

            Deriv dx = p2[secondIndex] - p1[firstIndex];
            // msg_info() << "dx value";
            // msg_info() << dx;
            // compute stress value
            m_weightedCoordinateDifference[i] = dx;
            m_springLength[i] = dx.norm();
            // msg_info() << "Spring length";
            // msg_info() << springLength[i];
            if (m_computeSpringsZeroLength[i] == 1) {
                m_initialSpringLength[i] = m_springLength[i];
                // msg_info() << "Spring zero length";
                // msg_info() << zeroLength[i] <<;
                m_computeSpringsZeroLength[i] = 0;
            }
            m_weightedCoordinateDifference[i] = m_weightedCoordinateDifference[i] / m_springLength[i];
            // msg_info() << "Weighted coordinate difference";
            // msg_info() << weightedCoordinateDifference[i];

            m_strainValue[i] = std::fabs(m_springLength[i] - m_initialSpringLength[i]) / m_initialSpringLength[i];
            double forceValue = PolynomialValue(0, m_strainValue[i]);
            m_strainSign[i] = m_springLength[i] - m_initialSpringLength[i] >= 0 ? 1.0 : compressionValue;

            // msg_info() << "Strain sign";
            // msg_info() << strainSign[i];

            // msg_info() << "Strain value";
            // msg_info() << strainValue[i];

            // msg_info() << "Force value";
            // msg_info() << forceValue;

            // msg_info() << "Applied force value";
            // msg_info() << forceValue * strainSign[i] * weightedCoordinateDifference[i];

            f1[firstIndex] += forceValue * m_strainSign[i] * m_weightedCoordinateDifference[i];
            f2[secondIndex] += -forceValue * m_strainSign[i] * m_weightedCoordinateDifference[i];

            ComputeJacobian(0, i);
        }
    }
    else
    {
        // msg_info() << "\n\n\n\nShow forces results";
        for (unsigned int i = 0; i < m_firstObjectIndices.size(); i++)
        {
            const unsigned int firstIndex = m_firstObjectIndices[i];
            const unsigned int secondIndex = m_secondObjectIndices[i];

            Deriv dx = p2[secondIndex] - p1[firstIndex];
            // msg_info() << "dx value";
            // msg_info() << dx;
            m_weightedCoordinateDifference[i] = dx;
            m_springLength[i] = dx.norm();
            // msg_info() << "Spring length value";
            // msg_info() << springLength[i];
            if (m_computeSpringsZeroLength[i] == 1) {
                m_initialSpringLength[i] = m_springLength[i];
                // msg_info() << "Spring zero length";
                // msg_info() << zeroLength[i];
                m_computeSpringsZeroLength[i] = 0;
            }
            m_weightedCoordinateDifference[i] = m_weightedCoordinateDifference[i] / m_springLength[i];


            m_strainValue[i] = std::fabs(m_springLength[i] - m_initialSpringLength[i]) / m_initialSpringLength[i];
            double forceValue = PolynomialValue(i, m_strainValue[i]);
            m_strainSign[i] = m_springLength[i] - m_initialSpringLength[i] >= 0 ? 1.0 : compressionValue;

            // msg_info() << "Strain sign";
            // msg_info() << strainSign[i];

            // msg_info() << "Strain value";
            // msg_info() << strainValue;

            // msg_info() << "Force value";
            // msg_info() << forceValue;

            // msg_info() << "Applied force value";
            // msg_info() << forceValue * strainSign[i] * weightedСoordinateDifference[i];

            f1[firstIndex] += forceValue * m_strainSign[i] * m_weightedCoordinateDifference[i];
            f2[secondIndex] += -forceValue * m_strainSign[i] * m_weightedCoordinateDifference[i];

            ComputeJacobian(i, i);
        }
    }
}



template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::ComputeJacobian(unsigned int stiffnessIndex, unsigned int springIndex)
{
    // msg_info() << "\n\nCompute derivative: ";
    // msg_info() << "spring length: " << m_springLength[springIndex];
    // Compute stiffness dF/dX for nonlinear case

    // msg_info() << "weighted difference: " << m_weightedCoordinateDifference[springIndex][0] << " " <<
    //                m_weightedCoordinateDifference[springIndex][1] << " " << m_weightedCoordinateDifference[springIndex][2];

    // compute polynomial result
    double polynomialForceRes = PolynomialValue(stiffnessIndex, m_strainValue[springIndex]) / m_springLength[springIndex];
    // msg_info() << "PolynomialForceRes: " << polynomialForceRes;

    double polynomialDerivativeRes = PolynomialDerivativeValue(stiffnessIndex, m_strainValue[springIndex]) / m_initialSpringLength[springIndex];
    // msg_info() << "PolynomialDerivativeRes: " << polynomialDerivativeRes;

    // compute data for Jacobian matrix
    JacobianMatrix& jacobMatrix = m_differential[springIndex];
    for(unsigned int firstIndex = 0; firstIndex < Dimension; firstIndex++)
    {
        for(unsigned int secondIndex = 0; secondIndex < Dimension; secondIndex++)
        {
            jacobMatrix[firstIndex][secondIndex] = (polynomialDerivativeRes - polynomialForceRes) *
                    m_weightedCoordinateDifference[springIndex][firstIndex] * m_weightedCoordinateDifference[springIndex][secondIndex];
        }
        jacobMatrix[firstIndex][firstIndex] += polynomialForceRes;
    }

    //for(unsigned int firstIndex = 0; firstIndex < Dimension; firstIndex++)
    //{
    //    for(unsigned int secondIndex = 0; secondIndex < Dimension; secondIndex++)
    //    {
    //        msg_info() << "for indices " << firstIndex << " and " << secondIndex << " the values is: " <<
    //                                                                          jacobMatrix[firstIndex][secondIndex];
    //    }
    //}
}


template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::addDForce(const core::MechanicalParams* mparams, DataVecDeriv& data_df1, DataVecDeriv& data_df2,
                                                       const DataVecDeriv& data_dx1, const DataVecDeriv& data_dx2)
{
    VecDeriv&        df1 = *data_df1.beginEdit();
    VecDeriv&        df2 = *data_df2.beginEdit();
    const VecDeriv&  dx1 =  data_dx1.getValue();
    const VecDeriv&  dx2 =  data_dx2.getValue();

    if (this->f_printLog.getValue())
        std::cout << "[" <<  this->getName() << "]: addDforce" << std::endl;

    Real kFactor = (Real)mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue());

    // msg_info() << "\n\nDerivative values: ";
    for (unsigned int index = 0; index < m_firstObjectIndices.size(); index++)
    {
        const JacobianMatrix& jacobMatrix = m_differential[index];
        Deriv forceDelta = jacobMatrix * (dx2[m_secondObjectIndices[index]] - dx1[m_firstObjectIndices[index]]);
        // msg_info() << "Spring stiffness derivative: " << forceDelta;

        df1[m_firstObjectIndices[index]] += forceDelta * kFactor;
        df2[m_secondObjectIndices[index]] -= forceDelta * kFactor;
    }
}


template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::draw(const core::visual::VisualParams* vparams)
{
    if (!((this->mstate1 == this->mstate2)?vparams->displayFlags().getShowForceFields():vparams->displayFlags().getShowInteractionForceFields()))
        return;
    const VecCoord& p1 =this->mstate1->read(core::ConstVecCoordId::position())->getValue();
    const VecCoord& p2 =this->mstate2->read(core::ConstVecCoordId::position())->getValue();

    const VecIndex& firstObjectIndices = d_firstObjectPoints.getValue();
    const VecIndex& secondObjectIndices = d_secondObjectPoints.getValue();

    std::vector< defaulttype::Vector3 > points;
    for (unsigned int i = 0; i < firstObjectIndices.size(); i++)
    {
        const unsigned int index1 = firstObjectIndices[i];
        points.push_back(p1[index1]);
        const unsigned int index2 = secondObjectIndices[i];
        points.push_back(p2[index2]);
    }

    if (d_showArrowSize.getValue() == 0 || d_drawMode.getValue() == 0)
    {
        vparams->drawTool()->drawLines(points, 1, d_springColor.getValue());
    }
    else if (d_drawMode.getValue() == 1)
    {
        const unsigned int numLines = points.size() / 2;
        for (unsigned int i = 0; i < numLines; ++i) {
            vparams->drawTool()->drawCylinder(points[2*i+1], points[2*i], d_showArrowSize.getValue(), d_springColor.getValue());
        }
    }
    else if (d_drawMode.getValue() == 2)
    {
        const unsigned int numLines = points.size() / 2;
        for (unsigned int i = 0; i < numLines; ++i) {
            vparams->drawTool()->drawArrow(points[2*i+1], points[2*i], d_showArrowSize.getValue(), d_springColor.getValue());
        }
    }

    // draw connected point indices
    defaulttype::Vec4f color(1.0, 1.0, 1.0, 1.0);

    Real scale = (vparams->sceneBBox().maxBBox() - vparams->sceneBBox().minBBox()).norm() * d_showIndicesScale.getValue();

    helper::vector<defaulttype::Vector3> positions;
    for (size_t i = 0; i < firstObjectIndices.size(); i++) {
        const unsigned int index = firstObjectIndices[i];
        positions.push_back(defaulttype::Vector3(p2[index][0], p2[index][1], p2[index][2] ));
    }

     vparams->drawTool()->draw3DText_Indices(positions, scale, color);
}



template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::addKToMatrix(const core::MechanicalParams* mparams /* PARAMS FIRST */,
                                                          const sofa::core::behavior::MultiMatrixAccessor* matrix )
{
    if (this->f_printLog.getValue())
        msg_info() << "[" <<  this->getName() << "]: addKToMatrix";

    sofa::helper::AdvancedTimer::stepBegin("restShapeSpringAddKToMatrix");

    Real kFact = (Real)mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue());
    unsigned int firstIndex = 0;
    unsigned int secondIndex = 0;

    if (this->mstate1 == this->mstate2)
    {
        sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref = matrix->getMatrix(this->mstate1);
        if (!mref) return;
        sofa::defaulttype::BaseMatrix* mat = mref.matrix;
        unsigned int offset = mref.offset;

        for (unsigned int index = 0; index < m_firstObjectIndices.size(); index++)
        {
            firstIndex = m_firstObjectIndices[index];
            secondIndex = m_secondObjectIndices[index];
            const JacobianMatrix& jacobMatrix = m_differential[index];

            // msg_info() << "\n\nDerivative values: ";
            for(unsigned int i = 0; i < Dimension; i++)
            {
                for (unsigned int j = 0; j < Dimension; j++)
                {
                    Real stiffnessDeriv = jacobMatrix[i][j] * kFact;
                    // msg_info() << "Spring stiffness derivative: " << stiffnessDeriv;
                    mat->add(offset + Dimension * firstIndex + i, offset + Dimension * firstIndex + j, -stiffnessDeriv);
                    mat->add(offset + Dimension * firstIndex + i, offset + Dimension * secondIndex + j, stiffnessDeriv);
                    mat->add(offset + Dimension * secondIndex + i, offset + Dimension * firstIndex + j, stiffnessDeriv);
                    mat->add(offset + Dimension * secondIndex + i, offset + Dimension * secondIndex + j, -stiffnessDeriv);
                }
            }
        }
        // msg_info() << "Draw matrix";
        // msg_info() << *mat;
    } else {
        sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref11 = matrix->getMatrix(this->mstate1);
        sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref22 = matrix->getMatrix(this->mstate2);
        sofa::core::behavior::MultiMatrixAccessor::InteractionMatrixRef mref12 = matrix->getMatrix(this->mstate1, this->mstate2);
        sofa::core::behavior::MultiMatrixAccessor::InteractionMatrixRef mref21 = matrix->getMatrix(this->mstate2, this->mstate1);
        if (!mref11 && !mref22 && !mref12 && !mref21) return;

        for (unsigned int index = 0; index < m_firstObjectIndices.size(); index++)
        {
            firstIndex = m_firstObjectIndices[index];
            secondIndex = m_secondObjectIndices[index];
            const JacobianMatrix& jacobMatrix = m_differential[index];

            // msg_info() << "\n\nDerivative values: ";
            for(unsigned int i = 0; i < Dimension; i++)
            {
                for (unsigned int j = 0; j < Dimension; j++)
                {
                    Real stiffnessDeriv = jacobMatrix[i][j] * kFact;
                    // msg_info() << "Spring stiffness derivative: " << stiffnessDeriv;
                    mref11.matrix->add(mref11.offset + Dimension * firstIndex + i, mref11.offset + Dimension * firstIndex + j, -stiffnessDeriv);
                    mref12.matrix->add(mref12.offRow + Dimension * firstIndex + i, mref12.offCol + Dimension * secondIndex + j, stiffnessDeriv);
                    mref21.matrix->add(mref21.offRow + Dimension * secondIndex + i, mref21.offCol + Dimension * firstIndex + j, stiffnessDeriv);
                    mref22.matrix->add(mref22.offset + Dimension * secondIndex + i, mref22.offset + Dimension * secondIndex + j, -stiffnessDeriv);
                }
            }
        }
    }

    sofa::helper::AdvancedTimer::stepEnd("restShapeSpringAddKToMatrix");
}


template<class DataTypes>
void PolynomialSpringsForceField<DataTypes>::addSubKToMatrix(const core::MechanicalParams* mparams,
                                                             const sofa::core::behavior::MultiMatrixAccessor* matrix,
                                                             const helper::vector<unsigned>& addSubIndex)
{
    Real kFact = (Real)mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue());
    unsigned int firstIndex = 0;
    unsigned int secondIndex = 0;

    if (this->mstate1 == this->mstate2)
    {
        sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref = matrix->getMatrix(this->mstate1);
        if (!mref) return;
        sofa::defaulttype::BaseMatrix* mat = mref.matrix;
        unsigned int offset = mref.offset;

        for (unsigned int index = 0; index < m_firstObjectIndices.size(); index++)
        {
            firstIndex = m_firstObjectIndices[index];
            secondIndex = m_secondObjectIndices[index];
            const JacobianMatrix& jacobMatrix = m_differential[index];

            bool contains = false;
            for (unsigned s = 0; s < addSubIndex.size() && !contains; s++) if (firstIndex == addSubIndex[s]) contains = true;
            if (!contains) continue;

            for(unsigned int i = 0; i < Dimension; i++)
            {
                for (unsigned int j = 0; j < Dimension; j++)
                {
                    Real stiffnessDeriv = jacobMatrix[i][j] * kFact;
                    mat->add(offset + Dimension * firstIndex + i, offset + Dimension * firstIndex + j, -stiffnessDeriv);
                    mat->add(offset + Dimension * firstIndex + i, offset + Dimension * secondIndex + j, stiffnessDeriv);
                    mat->add(offset + Dimension * secondIndex + i, offset + Dimension * firstIndex + j, stiffnessDeriv);
                    mat->add(offset + Dimension * secondIndex + i, offset + Dimension * secondIndex + j, -stiffnessDeriv);
                }
            }
        }
    } else {
        sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref11 = matrix->getMatrix(this->mstate1);
        sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref22 = matrix->getMatrix(this->mstate2);
        sofa::core::behavior::MultiMatrixAccessor::InteractionMatrixRef mref12 = matrix->getMatrix(this->mstate1, this->mstate2);
        sofa::core::behavior::MultiMatrixAccessor::InteractionMatrixRef mref21 = matrix->getMatrix(this->mstate2, this->mstate1);
        if (!mref11 && !mref22 && !mref12 && !mref21) return;

        for (unsigned int index = 0; index < m_firstObjectIndices.size(); index++)
        {
            firstIndex = m_firstObjectIndices[index];
            secondIndex = m_secondObjectIndices[index];
            const JacobianMatrix& jacobMatrix = m_differential[index];

            bool contains = false;
            for (unsigned s = 0; s < addSubIndex.size() && !contains; s++) if (firstIndex == addSubIndex[s]) contains = true;
            if (!contains) continue;

            for(unsigned int i = 0; i < Dimension; i++)
            {
                for (unsigned int j = 0; j < Dimension; j++)
                {
                    Real stiffnessDeriv = jacobMatrix[i][j] * kFact;
                    mref11.matrix->add(mref11.offset + Dimension * firstIndex + i, mref11.offset + Dimension * firstIndex + j, -stiffnessDeriv);
                    mref12.matrix->add(mref12.offRow + Dimension * firstIndex + i, mref12.offCol + Dimension * secondIndex + j, stiffnessDeriv);
                    mref21.matrix->add(mref21.offRow + Dimension * secondIndex + i, mref21.offCol + Dimension * firstIndex + j, stiffnessDeriv);
                    mref22.matrix->add(mref22.offset + Dimension * secondIndex + i, mref22.offset + Dimension * secondIndex + j, -stiffnessDeriv);
                }
            }
        }
    }
}



template<class DataTypes>
double PolynomialSpringsForceField<DataTypes>::PolynomialValue(unsigned int springIndex, double strainValue)
{
    helper::ReadAccessor<Data<VecReal>> vPolynomialStiffness = d_polynomialStiffness;
    helper::ReadAccessor<Data<helper::vector<unsigned int> >> vPolynomialDegree = d_polynomialDegree;

    double highOrderStrain = 1.0;
    double result = 0.0;
    for (size_t degreeIndex = 0; degreeIndex < vPolynomialDegree[springIndex]; degreeIndex++) {
        highOrderStrain *= strainValue;
        result += vPolynomialStiffness[m_polynomialsMap[springIndex][degreeIndex]] * highOrderStrain;
    }
    // msg_info() << "Polynomial data: ";
    //for (size_t polynomialIndex = 0; polynomialIndex < vPolynomialDegree[springIndex]; polynomialIndex++) {
    //    msg_info() << vPolynomialStiffness[m_polynomialsMap[springIndex][polynomialIndex]] << " ";
    //}

    return result;
}


template<class DataTypes>
double PolynomialSpringsForceField<DataTypes>::PolynomialDerivativeValue(unsigned int springIndex, double strainValue)
{
    helper::ReadAccessor<Data<VecReal>> vPolynomialStiffness = d_polynomialStiffness;
    helper::ReadAccessor<Data<helper::vector<unsigned int> >> vPolynomialDegree = d_polynomialDegree;

    double highOrderStrain = 1.0;
    double result = 0.0;
    for (size_t degreeIndex = 1; degreeIndex < vPolynomialDegree[springIndex]; degreeIndex++) {
        result += degreeIndex * vPolynomialStiffness[m_polynomialsMap[springIndex][degreeIndex - 1]] * highOrderStrain;
        // msg_info() << "Result: " << result;
        highOrderStrain *= strainValue;
    }
    // msg_info() << "Polynomial derivative data: ";
    //for (size_t polynomialIndex = 0; polynomialIndex < vPolynomialDegree[springIndex]; polynomialIndex++) {
    //    msg_info() << vPolynomialStiffness[m_polynomialsMap[springIndex][polynomialIndex]] << " ";
    //}

    return result;
}



} // namespace interactionforcefield

} // namespace component

} // namespace sofa

#endif // SOFA_COMPONENT_INTERACTIONFORCEFIELD_POLYNOMIAL_SPRINGS_FORCEFIELD_INL
