//
// Copyright (c) 2007 Ole Andr� Vadla Ravn�s <oleavr@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "stdafx.h"
#include "overlapped.h"

#define ENTER_OPS() EnterCriticalSection(&m_opsCriticalSection)
#define LEAVE_OPS() LeaveCriticalSection(&m_opsCriticalSection)

HANDLE COverlappedManager::m_opsChanged = 0;

CRITICAL_SECTION COverlappedManager::m_opsCriticalSection;
OVector<COverlappedOperation *>::Type COverlappedManager::m_operations;

void
COverlappedManager::Init()
{
	m_opsChanged = CreateEvent(NULL, FALSE, FALSE, NULL);

	InitializeCriticalSection(&m_opsCriticalSection);

	CreateThread(NULL, 0, MonitorThreadFunc, NULL, 0, NULL);
}

void
COverlappedManager::TrackOperation(OVERLAPPED **overlapped, void *data, OperationCompleteHandler handler)
{
	// FIXME: do garbage-collection here by having the client of this API provide
	//        a unique context id, i.e. socket handle + direction...
	COverlappedOperation *op = new COverlappedOperation(*overlapped, data, handler);
	*overlapped = op->GetRealOverlapped();

	ENTER_OPS();

	m_operations.push_back(op);
	SetEvent(m_opsChanged);

	LEAVE_OPS();
}

DWORD
COverlappedManager::MonitorThreadFunc(void *arg)
{
	while (true)
	{
		HANDLE *handles;
		COverlappedOperation **operations;
		unsigned int maxHandleCount, handleCount;

		// Make a list of operations not yet completed
		ENTER_OPS();

		maxHandleCount = 1 + static_cast<unsigned int>(m_operations.size());
        handles = (HANDLE *) AllocUtils::Malloc(sizeof(HANDLE) * maxHandleCount);
		operations = (COverlappedOperation **) AllocUtils::Malloc(sizeof(COverlappedOperation *) * maxHandleCount);

		handles[0] = m_opsChanged;
		operations[0] = NULL;

		handleCount = 1;

		for (unsigned int i = 0; i < m_operations.size(); i++)
		{
			COverlappedOperation *op = m_operations[i];
			if (!op->HasCompleted())
			{
				handles[handleCount] = op->GetRealOverlapped()->hEvent;
				operations[handleCount] = op;

				handleCount++;
			}
		}

		LEAVE_OPS();

		// Wait for events to be triggered
		bool operationsChanged = false;
		while (!operationsChanged)
		{
			DWORD result = WaitForMultipleObjects(handleCount, handles, FALSE, INFINITE);

			if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handleCount)
			{
				operationsChanged = true;

				for (unsigned int i = result - WAIT_OBJECT_0; i < handleCount; i++)
				{
					if (i > 0 && WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0)
					{
						operations[i]->HandleCompletion();
					}
				}
			}
		}

		AllocUtils::Free(handles);
		AllocUtils::Free(operations);
	}

	return 0;
}

COverlappedOperation::COverlappedOperation(OVERLAPPED *clientOverlapped, void *data, OperationCompleteHandler handler)
	: m_clientOverlapped(clientOverlapped), m_data(data), m_completionHandled(false), m_handler(handler)
{
	m_realOverlapped = (OVERLAPPED *) AllocUtils::Malloc(sizeof(OVERLAPPED));
	memset(m_realOverlapped, 0, sizeof(OVERLAPPED));
	m_realOverlapped->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

void
COverlappedOperation::HandleCompletion()
{
	if (!m_completionHandled)
	{
		m_completionHandled = true;
		m_handler(this);
	}
}
