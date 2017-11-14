#include "RAS_DisplayArrayStorage.h"
#include "RAS_StorageVbo.h"

RAS_DisplayArrayStorage::RAS_DisplayArrayStorage(RAS_IDisplayArray *array)
	:m_vbo(new RAS_StorageVbo(array))
{
}

RAS_DisplayArrayStorage::~RAS_DisplayArrayStorage()
{
}

RAS_StorageVbo *RAS_DisplayArrayStorage::GetVbo() const
{
	return m_vbo.get();
}

void RAS_DisplayArrayStorage::UpdateVertexData()
{
	m_vbo->UpdateVertexData();
}

void RAS_DisplayArrayStorage::UpdateSize()
{
	m_vbo->UpdateSize();
}

unsigned int *RAS_DisplayArrayStorage::GetIndexMap()
{
	return m_vbo->GetIndexMap();
}

void RAS_DisplayArrayStorage::FlushIndexMap()
{
	m_vbo->FlushIndexMap();
}

void RAS_DisplayArrayStorage::IndexPrimitives()
{
	m_vbo->IndexPrimitives();
}

void RAS_DisplayArrayStorage::IndexPrimitivesInstancing(unsigned int numslots)
{
	m_vbo->IndexPrimitivesInstancing(numslots);
}

void RAS_DisplayArrayStorage::IndexPrimitivesBatching(const std::vector<void *>& indices, const std::vector<int>& counts)
{
	m_vbo->IndexPrimitivesBatching(indices, counts);
}
