/*
 * 'Expression Controller enables to calculate an expression that wires inputs to output
 *
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/GameLogic/SCA_ExpressionController.cpp
 *  \ingroup gamelogic
 */


#include "SCA_ExpressionController.h"
#include "SCA_ISensor.h"
#include "SCA_LogicManager.h"
#include "EXP_BoolValue.h"
#include "EXP_InputParser.h"
#include "mathfu.h" // for FuzzyZero

#include "CM_Message.h"

/* ------------------------------------------------------------------------- */
/* Native functions                                                          */
/* ------------------------------------------------------------------------- */

SCA_ExpressionController::SCA_ExpressionController(SCA_IObject* gameobj,
												   const std::string& exprtext)
	:SCA_IController(gameobj),
	m_exprText(exprtext),
	m_exprCache(nullptr)
{
}



SCA_ExpressionController::~SCA_ExpressionController()
{
	if (m_exprCache)
		m_exprCache->Release();
}



EXP_Value* SCA_ExpressionController::GetReplica()
{
	SCA_ExpressionController* replica = new SCA_ExpressionController(*this);
	replica->m_exprText = m_exprText;
	replica->m_exprCache = nullptr;
	// this will copy properties and so on...
	replica->ProcessReplica();

	return replica;
}


// Forced deletion of precalculated expression to break reference loop
// Use this function when you know that you won't use the sensor anymore
void SCA_ExpressionController::Delete()
{
	if (m_exprCache)
	{
		m_exprCache->Release();
		m_exprCache = nullptr;
	}
	Release();
}


void SCA_ExpressionController::Trigger(SCA_LogicManager* logicmgr)
{

	bool expressionresult = false;
	if (!m_exprCache)
	{
		EXP_Parser parser;
		parser.SetContext(this->AddRef());
		m_exprCache = parser.ProcessText(m_exprText);
	}
	if (m_exprCache)
	{
		EXP_Value* value = m_exprCache->Calculate();
		if (value)
		{
			if (value->IsError())
			{
				CM_LogicBrickError(this, value->GetText());
			} else
			{
				float num = (float)value->GetNumber();
				expressionresult = !mt::FuzzyZero(num);
			}
			value->Release();

		}
	}

	for (std::vector<SCA_IActuator*>::const_iterator i=m_linkedactuators.begin();
	!(i==m_linkedactuators.end());i++)
	{
		SCA_IActuator* actua = *i;
		logicmgr->AddActiveActuator(actua,expressionresult);
	}
}



EXP_Value* SCA_ExpressionController::FindIdentifier(const std::string& identifiername)
{

	EXP_Value* identifierval = nullptr;

	for (std::vector<SCA_ISensor*>::const_iterator is=m_linkedsensors.begin();
	!(is==m_linkedsensors.end());is++)
	{
		SCA_ISensor* sensor = *is;
		if (sensor->GetName() == identifiername)
		{
			identifierval = new EXP_BoolValue(sensor->GetState());
			//identifierval = sensor->AddRef();
			break;
		}

		//if (!sensor->IsPositiveTrigger())
		//{
		//	sensorresult = false;
		//	break;
		//}
	}

	if (identifierval)
		return identifierval;

	return  GetParent()->FindIdentifier(identifiername);

}
