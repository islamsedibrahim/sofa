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
#ifndef SOFA_COMPONENT_FORCEFIELD_POLYNOMIAL_RESTSHAPESPRINGSFORCEFIELD_INL
#define SOFA_COMPONENT_FORCEFIELD_POLYNOMIAL_RESTSHAPESPRINGSFORCEFIELD_INL


#include <sofa/core/behavior/ForceField.inl>
#include "PolynomialRestShapeSpringsForceField.h"
#include <sofa/core/visual/VisualParams.h>
#include <sofa/helper/system/config.h>
#include <sofa/defaulttype/VecTypes.h>
#include <sofa/defaulttype/RigidTypes.h>
#include <sofa/helper/gl/template.h>
#include <sofa/simulation/AnimateBeginEvent.h>
#include <assert.h>
#include <iostream>
#include <fstream>
#include <sofa/helper/AdvancedTimer.h>


namespace sofa
{

namespace component
{

namespace forcefield
{

template<class DataTypes>
PolynomialRestShapeSpringsForceField<DataTypes>::PolynomialRestShapeSpringsForceField()
    : points(initData(&points, "points", "points controlled by the rest shape springs"))
    , d_polynomialStiffness(initData(&d_polynomialStiffness, "polynomialStiffness", "coefficients for all spring polynomials"))
    , d_polynomialDegree(initData(&d_polynomialDegree, "polynomialDegree", "vector of values that show polynomials degrees"))
    , external_points(initData(&external_points, "external_points", "points from the external Mechancial State that define the rest shape springs"))
    , d_recomputeIndices(initData(&d_recomputeIndices, false, "recompute_indices", "Recompute indices (should be false for BBOX)"))
    , d_drawSpring(initData(&d_drawSpring,false,"drawSpring","draw Spring"))
    , d_springColor(initData(&d_springColor, defaulttype::RGBAColor(0.0f, 1.0f, 0.0f, 1.0f), "springColor","spring color"))
    , d_showIndicesScale(initData(&d_showIndicesScale, (float)0.02, "showIndicesScale", "Scale for indices display. (default=0.02)"))
    , d_zeroLength(initData(&d_zeroLength,"initialLength","initial virtual length of the spring"))
    , d_smoothShift(initData(&d_smoothShift,double(0.0),"smoothShift","denominator correction adding shift value"))
    , d_smoothScale(initData(&d_smoothScale,double(1.0),"smoothScale","denominator correction adding scale"))
    , restMState(initLink("external_rest_shape", "rest_shape can be defined by the position of an external Mechanical State"))
{        
}


template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::bwdInit()
{
    core::behavior::ForceField<DataTypes>::init();

    if (d_polynomialStiffness.getValue().empty())
    {
        std::cout << "ExtendedRestShapeSpringForceField : No stiffness is defined, assuming equal stiffness on each node, k = 100.0 " << std::endl;

        VecReal stiffs;
        stiffs.push_back(100.0);
        d_polynomialStiffness.setValue(stiffs);
    }

    if (d_zeroLength.getValue().empty())
    {
        VecReal dist;
        dist.push_back(1.0);
        d_zeroLength.setValue(dist);
    }

    if (restMState.get() == nullptr)
    {
        useRestMState = false;
        msg_info() << "no external rest shape used";

        if (!restMState.empty())
        {
            msg_warning() << "external_rest_shape in node " << this->getContext()->getName() << " not found";
        }
    }
    else
    {
        msg_info() << "external rest shape used";
        useRestMState = true;
    }

    if (useRestMState)
    {
        msg_info() << "[" << this->getName() << "]: using the external state " << restMState->getName();
    }
    else
    {
        msg_info() << "[" << this->getName() << "]: using the rest state " << this->mstate->getName();
    }

    recomputeIndices();

    core::behavior::BaseMechanicalState* state = this->getContext()->getMechanicalState();
    if(!state)
    {
        msg_warning() << "MechanicalState of the current context returns null pointer";
    }

    // read and fill polynomial parameters
    if (d_polynomialDegree.getValue().empty()) {
        helper::WriteAccessor<Data<helper::vector<unsigned int>>> vPolynomialWriteDegree = d_polynomialDegree;
        vPolynomialWriteDegree.push_back(1);
    }

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

    // recreate derivatives matrices
    m_differential.resize(m_indices.size());

    m_directionSpringLength.resize(m_indices.size());
    m_strainValue.resize(m_indices.size());
    m_weightedCoordinateDifference.resize(m_indices.size());
    m_coordinateSquaredNorm.resize(m_indices.size());
}


template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::recomputeIndices()
{
    m_indices.clear();
    m_ext_indices.clear();

    for (unsigned int i = 0; i < points.getValue().size(); i++)
        m_indices.push_back(points.getValue()[i]);

    for (unsigned int i = 0; i < external_points.getValue().size(); i++)
        m_ext_indices.push_back(external_points.getValue()[i]);

    if (m_indices.empty())
    {
        //	msg_info() << "in PolynomialRestShapeSpringForceField no point are defined, default case: points = all points ";
        for (unsigned int i = 0; i < (unsigned)this->mstate->getSize(); i++) {
            m_indices.push_back(i);
        }
    }

    if (m_ext_indices.empty())
    {
        // msg_info() << "in PolynomialRestShapeSpringForceField no external_points are defined, default case: points = all points ";

        if (useRestMState)
        {
            for (unsigned int i = 0; i < (unsigned)getExtPosition()->getValue().size(); i++)
            {
                m_ext_indices.push_back(i);
            }
        }
        else
        {
            for (unsigned int i = 0; i < (unsigned)this->mstate->getSize(); i++)
            {
                m_ext_indices.push_back(i);
            }
        }
    }

    if (m_indices.size() > m_ext_indices.size())
    {
        msg_error() << "Error : the dimention of the source and the targeted points are different ";
        m_indices.clear();
        m_ext_indices.clear();
    }
}


template<class DataTypes>
const typename PolynomialRestShapeSpringsForceField<DataTypes>::DataVecCoord* PolynomialRestShapeSpringsForceField<DataTypes>::getExtPosition() const
{
    return (useRestMState ? restMState->read(core::VecCoordId::position()) : this->mstate->read(core::VecCoordId::restPosition()));
}


template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::addForce(const core::MechanicalParams* mparams, DataVecDeriv& f,
                                                               const DataVecCoord& x, const DataVecDeriv& v)
{
    SOFA_UNUSED(mparams);
    SOFA_UNUSED(v);

    helper::WriteAccessor< DataVecDeriv > f1 = f;
    helper::ReadAccessor< DataVecCoord > p1 = x;
    helper::ReadAccessor< DataVecCoord > p0 = *getExtPosition();

    // msg_info() << this->getName() << " P1 = " << p1;
    // msg_info() << this->getName() << " P0 = " << p0;
    // msg_info() << this->getName() << " F = " << f1;

    if (this->f_printLog.getValue())
        msg_info() << "[" <<  this->getName() << "]: ";

    const VecReal& zeroLength = d_zeroLength.getValue();
    f1.resize(p1.size());

    if (d_recomputeIndices.getValue())
    {
        recomputeIndices();
    }

    if ( d_polynomialDegree.getValue().size() != m_indices.size() )
    {
        // msg_warning() << "WARNING : stiffness is not defined on each point, first stiffness is used";
        for (unsigned int i = 0; i < m_indices.size(); i++)
        {
            const unsigned int index = m_indices[i];
            unsigned int ext_index = m_indices[i];
            if(useRestMState)
                ext_index= m_ext_indices[i];

            Deriv dx = p1[index] - p0[ext_index];
            m_coordinateSquaredNorm[i] = dot(dx, dx);
            // msg_info() << "p1 value";
            // msg_info() << p1[index];
            // msg_info() << "dx value";
            // msg_info() << dx;

            // to compute stress value use original spring length
            double springLength = dx.norm();
            // msg_info() << "Spring length value";
            // msg_info() << springLength[i];
            m_strainValue[i] = springLength / (i < zeroLength.size() ? zeroLength[i] : zeroLength[0]);
            double forceValue = PolynomialValue(0, m_strainValue[i]);

            // msg_info() << "Strain value";
            // msg_info() << strainValue;

            // msg_info() << "Force value";
            // msg_info() << forceValue;

            // to compute direction use the modified length denominator: dx^2 + exp^(shift - scale * dx^2)
            double squaredDenominator = dot(dx, dx);
            squaredDenominator += exp(d_smoothShift.getValue() - d_smoothScale.getValue() * dot(dx, dx));
            // msg_info() << "Denominator value";
            // msg_info() << std::sqrt(squaredDenominator);

            // compute the length with modified values
            m_directionSpringLength[i] = std::sqrt(squaredDenominator);
            m_weightedCoordinateDifference[i] = dx;
            m_weightedCoordinateDifference[i] = m_weightedCoordinateDifference[i] / m_directionSpringLength[i];

            f1[index] -=  forceValue * m_weightedCoordinateDifference[i];
            // msg_info() << "Applied force value";
            // msg_info() << forceValue * m_weightedCoordinateDifference[i];

            ComputeJacobian(0, i);
        }
    }
    else
    {
        // msg_info() << "\n\n\n\nShow forces results";
        for (unsigned int i=0; i<m_indices.size(); i++)
        {
            const unsigned int index = m_indices[i];
            unsigned int ext_index = m_indices[i];
            if(useRestMState)
                ext_index= m_ext_indices[i];

            Deriv dx = p1[index] - p0[ext_index];
            m_coordinateSquaredNorm[i] = dot(dx, dx);
            // msg_info() << "p1 value";
            // msg_info() << p1[index];
            // msg_info() << "dx value";
            // msg_info() << dx;

            // to compute stress value use original spring length
            double springLength = dx.norm();
            // msg_info() << "Spring length value";
            // msg_info() << springLength[i];
            m_strainValue[i] = springLength / (i < zeroLength.size() ? zeroLength[i] : zeroLength[0]);
            double forceValue = PolynomialValue(i, m_strainValue[i]);

            // msg_info() << "Strain value";
            // msg_info() << strainValue;

            // msg_info() << "Force value";
            // msg_info() << forceValue;

            // to compute direction use the modified length denominator: dx^2 + exp^(shift - scale * dx^2)
            double squaredDenominator = dot(dx, dx);
            squaredDenominator += exp(d_smoothShift.getValue() - d_smoothScale.getValue() * dot(dx, dx));
            // msg_info() << "Denominator value";
            // msg_info() << std::sqrt(squaredDenominator);

            // compute the length with modified values
            m_directionSpringLength[i] = std::sqrt(squaredDenominator);
            m_weightedCoordinateDifference[i] = dx;
            m_weightedCoordinateDifference[i] = m_weightedCoordinateDifference[i] / m_directionSpringLength[i];

            f1[index] -= forceValue * m_weightedCoordinateDifference[i];
            // msg_info() << "Applied force value";
            // msg_info() << forceValue / springLengthForDirection[i] * dx;

            ComputeJacobian(i, i);
        }
    }
}



template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::ComputeJacobian(unsigned int stiffnessIndex, unsigned int springIndex)
{
    // msg_info() << "\n\nCompute derivative: ";
    // msg_info() << "spring length: " << m_directionSpringLength[springIndex];
    // Compute stiffness dF/dX for nonlinear case

    // msg_info() << "weighted difference: " << m_weightedCoordinateDifference[springIndex][0] << " " <<
    //                m_weightedCoordinateDifference[springIndex][1] << " " << m_weightedCoordinateDifference[springIndex][2];

    const VecReal& zeroLength = d_zeroLength.getValue();

    // NOTE: Since we compute derivatives only for one of mechanical models we take only diagonal of jacobian matrix

    // compute polynomial result
    double polynomialForceRes = PolynomialValue(stiffnessIndex, m_strainValue[springIndex]) / m_directionSpringLength[springIndex];
    // msg_info() << "PolynomialForceRes: " << polynomialForceRes;

    double polynomialDerivativeRes = PolynomialDerivativeValue(stiffnessIndex, m_strainValue[springIndex]) /
            (springIndex < zeroLength.size() ? zeroLength[springIndex] : zeroLength[0]);
    // msg_info() << "PolynomialDerivativeRes: " << polynomialDerivativeRes;

    double exponentialDerivative = 1.0 - d_smoothScale.getValue() *
            exp(d_smoothShift.getValue() - d_smoothScale.getValue() * m_coordinateSquaredNorm[springIndex]);

    // compute data for Jacobian matrix
    JacobianVector& jacobVector = m_differential[springIndex];
    for(unsigned int index = 0; index < Coord::total_size; index++)
    {
        jacobVector[index] = (polynomialDerivativeRes - polynomialForceRes) * exponentialDerivative *
                    m_weightedCoordinateDifference[springIndex][index] * m_weightedCoordinateDifference[springIndex][index] + polynomialForceRes;
    }

    //for(unsigned int index = 0; index < Coord::total_size; index++)
    //{
    //    msg_info() << "for indices " << index << " the values is: " << jacobVector[index];
    //}
}



template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::addDForce(const core::MechanicalParams* mparams, DataVecDeriv& df, const DataVecDeriv& dx)
{
    if (this->f_printLog.getValue())
        msg_info() << "[" <<  this->getName() << "]: addDforce";

    helper::WriteAccessor< DataVecDeriv > df1 = df;
    helper::ReadAccessor< DataVecDeriv > dx1 = dx;
    Real kFactor = (Real)mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue());

    // msg_info() << "\n\nDerivative values: ";
    for (unsigned int index = 0; index < m_indices.size(); index++)
    {
        const JacobianVector& jacobVector = m_differential[index];

        for(unsigned int coordIndex = 0; coordIndex < Coord::total_size; coordIndex++)
        {
            df1[m_indices[index]][coordIndex] += jacobVector[coordIndex] * dx1[m_indices[index]][coordIndex] * kFactor;
        }
    }
}



template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::draw(const core::visual::VisualParams* vparams)
{
    if (!vparams->displayFlags().getShowForceFields() || !d_drawSpring.getValue())
        return;

    helper::ReadAccessor< DataVecCoord > p0 = *getExtPosition();
    helper::ReadAccessor< DataVecCoord > p  = this->mstate->read(core::VecCoordId::position());

    const VecIndex& indices = m_indices;
    const VecIndex& ext_indices = (useRestMState ? m_ext_indices : m_indices);

    std::vector< defaulttype::Vector3 > points;

    for (unsigned int i=0; i<indices.size(); i++)
    {
        const unsigned int index = indices[i];
        const unsigned int ext_index = ext_indices[i];
        points.push_back(p[index]);
        points.push_back(p0[ext_index]);
    }

    vparams->drawTool()->saveLastState();
    vparams->drawTool()->setLightingEnabled(false);

    vparams->drawTool()->drawLines(points, 5, d_springColor.getValue());

    vparams->drawTool()->restoreLastState();


    // draw connected point indices
    defaulttype::Vec4f color(1.0, 1.0, 1.0, 1.0);

    Real scale = (vparams->sceneBBox().maxBBox() - vparams->sceneBBox().minBBox()).norm() * d_showIndicesScale.getValue();

    helper::vector<defaulttype::Vector3> positions;
    for (size_t i = 0; i < indices.size(); i++) {
        const unsigned int index = indices[i];
        positions.push_back(defaulttype::Vector3(p0[index][0], p0[index][1], p0[index][2] ));
    }

    vparams->drawTool()->draw3DText_Indices(positions, scale, color);
}



template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::addKToMatrix(const core::MechanicalParams* mparams,
                                                                   const sofa::core::behavior::MultiMatrixAccessor* matrix )
{    
    if (this->f_printLog.getValue())
        std::cout << "[" <<  this->getName() << "]: addKToMatrix" << std::endl;

    sofa::helper::AdvancedTimer::stepBegin("restShapePolynomialSpringAddKToMatrix");

    sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref = matrix->getMatrix(this->mstate);
    sofa::defaulttype::BaseMatrix* mat = mref.matrix;
    unsigned int offset = mref.offset;
    Real kFact = (Real)mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue());

    unsigned int curIndex = 0;
    const int Dimension = Coord::total_size;

    // msg_info() << "\n\nDerivative values: ";
    for (unsigned int index = 0; index < m_indices.size(); index++)
    {
        curIndex = m_indices[index];
        const JacobianVector& jacobVector = m_differential[index];

        for(unsigned int i = 0; i < Dimension; i++)
        {
            mat->add(offset + Dimension * curIndex + i, offset + Dimension * curIndex + i, -kFact * jacobVector[i]);
        }
    }
    // msg_info() << "Draw matrix";
    // msg_info() << *mat;

    sofa::helper::AdvancedTimer::stepEnd("restShapePolynomialSpringAddKToMatrix");
}


template<class DataTypes>
void PolynomialRestShapeSpringsForceField<DataTypes>::addSubKToMatrix(const core::MechanicalParams* mparams,
                                                                      const sofa::core::behavior::MultiMatrixAccessor* matrix,
                                                                      const helper::vector<unsigned> & addSubIndex )
{
    sofa::core::behavior::MultiMatrixAccessor::MatrixRef mref = matrix->getMatrix(this->mstate);
    sofa::defaulttype::BaseMatrix* mat = mref.matrix;
    unsigned int offset = mref.offset;
    Real kFact = (Real)mparams->kFactorIncludingRayleighDamping(this->rayleighStiffness.getValue());

    unsigned int curIndex = 0;
    const int Dimension = Coord::total_size;

    // msg_info() << "\n\nDerivative values: ";
    for (unsigned int index = 0; index < m_indices.size(); index++)
    {
        curIndex = m_indices[index];
        const JacobianVector& jacobVector = m_differential[index];
        bool contains=false;
        for (unsigned s=0;s<addSubIndex.size() && !contains;s++) if (curIndex==addSubIndex[s]) contains=true;
        if (!contains) continue;

        for(unsigned int i = 0; i < Dimension; i++)
        {
            mat->add(offset + Dimension * curIndex + i, offset + Dimension * curIndex + i, -kFact * jacobVector[i]);
        }
    }
    // msg_info() << "Draw matrix";
    // msg_info() << *mat;
}



template<class DataTypes>
double PolynomialRestShapeSpringsForceField<DataTypes>::PolynomialValue(unsigned int springIndex, double strainValue)
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
double PolynomialRestShapeSpringsForceField<DataTypes>::PolynomialDerivativeValue(unsigned int springIndex, double strainValue)
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



} // namespace forcefield

} // namespace component

} // namespace sofa

#endif // SOFA_COMPONENT_FORCEFIELD_POLYNOMIAL_RESTSHAPESPRINGSFORCEFIELD_INL



