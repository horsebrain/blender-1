/*
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
 * Contributor(s): Tristan Porteries.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file RAS_BoundingBox.cpp
 *  \ingroup bgerast
 */

#include "RAS_BoundingBox.h"
#include "RAS_BoundingBoxManager.h"

#include "CM_List.h"

RAS_BoundingBox::RAS_BoundingBox(RAS_BoundingBoxManager *manager)
	:m_modified(false),
	m_aabbMin(mt::zero3),
	m_aabbMax(mt::zero3),
	m_users(0),
	m_manager(manager)
{
	BLI_assert(m_manager);
	m_manager->m_boundingBoxList.push_back(this);
}

RAS_BoundingBox::~RAS_BoundingBox()
{
}

RAS_BoundingBox *RAS_BoundingBox::GetReplica()
{
	RAS_BoundingBox *boundingBox = new RAS_BoundingBox(*this);
	boundingBox->ProcessReplica();
	return boundingBox;
}

void RAS_BoundingBox::ProcessReplica()
{
	m_users = 1;
	m_manager->m_boundingBoxList.push_back(this);
}

void RAS_BoundingBox::AddUser()
{
	++m_users;
	/* No one was using this bounding box previously. Then add it to the active
	 * bounding box list in the manager.*/
	if (m_users == 1) {
		m_manager->m_activeBoundingBoxList.push_back(this);
	}
}

void RAS_BoundingBox::RemoveUser()
{
	--m_users;
	BLI_assert(m_users >= 0);

	/* Some one was using this bounding box previously. Then remove it from the
	 * active bounding box list. */
	if (m_users == 0) {
		CM_ListRemoveIfFound(m_manager->m_activeBoundingBoxList, this);
	}
}

void RAS_BoundingBox::SetManager(RAS_BoundingBoxManager *manager)
{
	m_manager = manager;
}

bool RAS_BoundingBox::GetModified() const
{
	return m_modified;
}

void RAS_BoundingBox::ClearModified()
{
	m_modified = false;
}

void RAS_BoundingBox::GetAabb(mt::vec3& aabbMin, mt::vec3& aabbMax) const
{
	aabbMin = m_aabbMin;
	aabbMax = m_aabbMax;
}

void RAS_BoundingBox::SetAabb(const mt::vec3& aabbMin, const mt::vec3& aabbMax)
{
	m_aabbMin = aabbMin;
	m_aabbMax = aabbMax;
	m_modified = true;
}

void RAS_BoundingBox::ExtendAabb(const mt::vec3& aabbMin, const mt::vec3& aabbMax)
{
	m_aabbMin = mt::vec3::Min(m_aabbMin, aabbMin);
	m_aabbMax = mt::vec3::Max(m_aabbMax, aabbMax);
	m_modified = true;
}

void RAS_BoundingBox::CopyAabb(RAS_BoundingBox *other)
{
	other->GetAabb(m_aabbMin, m_aabbMax);
	m_modified = true;
}

void RAS_BoundingBox::Update(bool force)
{
}

RAS_MeshBoundingBox::RAS_MeshBoundingBox(RAS_BoundingBoxManager *manager, const RAS_IDisplayArrayList& displayArrayList)
	:RAS_BoundingBox(manager),
	m_displayArrayList(displayArrayList)
{
}

RAS_MeshBoundingBox::~RAS_MeshBoundingBox()
{
}

RAS_BoundingBox *RAS_MeshBoundingBox::GetReplica()
{
	RAS_MeshBoundingBox *boundingBox = new RAS_MeshBoundingBox(*this);
	boundingBox->m_users = 0;
	return boundingBox;
}

void RAS_MeshBoundingBox::Update(bool force)
{
	bool modified = false;
	// Detect if a display array was modified.
	for (RAS_IDisplayArray *array : m_displayArrayList) {
		if (array->GetModifiedFlag() & RAS_IDisplayArray::AABB_MODIFIED) {
			modified = true;
			break;
		}
	}

	if (!modified && !force) {
		return;
	}

	m_aabbMin = mt::vec3(FLT_MAX);
	m_aabbMax = mt::vec3(-FLT_MAX);

	for (RAS_IDisplayArray *displayArray : m_displayArrayList) {
		// For each vertex.
		for (unsigned int i = 0, size = displayArray->GetVertexCount(); i < size; ++i) {
			RAS_IVertex *vert = displayArray->GetVertex(i);
			const mt::vec3 vertPos = vert->xyz();

			m_aabbMin = mt::vec3::Min(m_aabbMin, vertPos);
			m_aabbMax = mt::vec3::Max(m_aabbMax, vertPos);
		}
	}

	m_modified = true;
}
