/*------------------------------------------------------------------------------
   Copyright � 2016-2017
   New Global Systems for Intelligent Transportation Management Corp.
   
   This file is part of SSAM.
  
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Affero General Public License for more details.
  
   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
------------------------------------------------------------------------------*/
// Dlg_Conflicts.cpp : implementation file
//

#include "stdafx.h"
#include <fstream>
#include "SSAMAPP.h"
#include "Dlg_Conflicts.h"
#include "afxdialogex.h"
#include "Conflict.h"
#include "Dlg_ConflictProperties.h"

// CDlg_Conflicts dialog

IMPLEMENT_DYNAMIC(CDlg_Conflicts, CDialog)

CDlg_Conflicts::CDlg_Conflicts(CWnd* pParent /*=NULL*/)
	: CDlg_Tab(pParent)
	, m_InitialClientSize(0, 0)
{

}

CDlg_Conflicts::CDlg_Conflicts(SP_SSAMDoc pSSAMDoc, CWnd* pParent /*=NULL*/)
	: CDlg_Tab(pParent)
	, m_pSSAMDoc (pSSAMDoc)
	, m_InitialClientSize(0, 0)
{

}

CDlg_Conflicts::~CDlg_Conflicts()
{
}

void CDlg_Conflicts::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_CONFLICTS, m_ListCtrl_Conflicts);
}

BOOL CDlg_Conflicts::OnInitDialog()
{
	CDialog::OnInitDialog();
	GetFilterStatus();
	
	m_ListCtrl_Conflicts.SetExtendedStyle(m_ListCtrl_Conflicts.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	
	// add the headers
	for(int m = 0; m < Conflict::NUM_MEASURES; m++)
	{
		m_ListCtrl_Conflicts.InsertColumn(m, _T(Conflict::MEASURE_LABEL[m].c_str()), LVCFMT_LEFT, 90, -1);
	}

	CRect clientRect;
	GetClientRect(&clientRect);
	m_InitialClientSize = clientRect.Size();

	m_ListCtrl_Conflicts.GetWindowRect(&m_InitialListRect);
	ScreenToClient(&m_InitialListRect);

	CWnd* pExportButton = GetDlgItem(IDC_BUTTON_EXPORT);
	if (pExportButton != NULL)
	{
		pExportButton->GetWindowRect(&m_InitialExportRect);
		ScreenToClient(&m_InitialExportRect);
	}
	return TRUE;
}

bool CDlg_Conflicts::SaveData()
{
	return true;
}

namespace
{
	struct ListRedrawGuard
	{
		CListCtrl& m_List;
		explicit ListRedrawGuard(CListCtrl& list) : m_List(list) { m_List.SetRedraw(FALSE); }
		~ListRedrawGuard() { m_List.SetRedraw(TRUE); m_List.Invalidate(); }
	private:
		ListRedrawGuard& operator=(const ListRedrawGuard&);
	};
}

void CDlg_Conflicts::SetData()
{
	GetFilterStatus();
	ListRedrawGuard redrawGuard(m_ListCtrl_Conflicts);
	m_ListCtrl_Conflicts.DeleteAllItems();
	
	int iRow = 0;
	for (std::list<SP_Conflict>::iterator it = m_pConflictList->begin();
		it != m_pConflictList->end(); ++it)
	{
		int iCol = 0;
		int nIdx = m_ListCtrl_Conflicts.InsertItem(iRow, _T((*it)->GetValueString(iCol).c_str()));
		for (iCol = 1; iCol < Conflict::NUM_MEASURES; iCol++)
		{
			m_ListCtrl_Conflicts.SetItemText(nIdx, iCol, _T((*it)->GetValueString(iCol).c_str()));
			
		}
		iRow++;
	}

	AutoSizeColumns();
}

void CDlg_Conflicts::AutoSizeColumns()
{
	CHeaderCtrl* pHeader = m_ListCtrl_Conflicts.GetHeaderCtrl();
	if (pHeader == NULL)
	{
		return;
	}

	for (int i = 0; i < pHeader->GetItemCount(); ++i)
	{
		m_ListCtrl_Conflicts.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
	}
}

BEGIN_MESSAGE_MAP(CDlg_Conflicts, CDialog)
	ON_BN_CLICKED(IDC_BUTTON_EXPORT, &CDlg_Conflicts::OnBnClickedButtonExport)
	ON_WM_SIZE()
END_MESSAGE_MAP()


// CDlg_Conflicts message handlers


void CDlg_Conflicts::OnBnClickedButtonExport()
{
	CFileDialog fdlg (FALSE, "*.csv", m_pSSAMDoc->GetCsvName().c_str(),OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT |OFN_ENABLESIZING | OFN_EXPLORER,
			"CSV (Comma delimited) (*.csv)|*.csv|",NULL,0,true);	

	if(fdlg.DoModal()==IDOK)
	{
		CString path = fdlg.GetPathName  ();
		std::ofstream csvFile(path);

		for(int m = 0; m < Conflict::NUM_MEASURES; m++)
		{
			csvFile << Conflict::MEASURE_LABEL[m] << ",";
		}
		csvFile << std::endl;

		for (std::list<SP_Conflict>::iterator it = m_pConflictList->begin();
		it != m_pConflictList->end(); ++it)
		{
			for (int iCol = 0; iCol < Conflict::NUM_MEASURES; iCol++)
			{
				csvFile << (*it)->GetValueString(iCol) << ",";
			
			}
			csvFile << std::endl;
		}
		csvFile.close();
	}
}

void CDlg_Conflicts::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);

	if (m_InitialClientSize.cx == 0 || !::IsWindow(m_ListCtrl_Conflicts.GetSafeHwnd()))
	{
		return;
	}

	const int dx = cx - m_InitialClientSize.cx;
	const int dy = cy - m_InitialClientSize.cy;
	int listWidth = m_InitialListRect.Width() + dx;
	int listHeight = m_InitialListRect.Height() + dy;

	if (listWidth < 0) listWidth = 0;
	if (listHeight < 0) listHeight = 0;

	m_ListCtrl_Conflicts.SetWindowPos(NULL,
		m_InitialListRect.left,
		m_InitialListRect.top,
		listWidth,
		listHeight,
		SWP_NOACTIVATE | SWP_NOZORDER);

	CWnd* pExportButton = GetDlgItem(IDC_BUTTON_EXPORT);
	if (pExportButton != NULL && ::IsWindow(pExportButton->GetSafeHwnd()))
	{
		pExportButton->SetWindowPos(NULL,
			m_InitialExportRect.left,
			m_InitialExportRect.top + dy,
			m_InitialExportRect.Width(),
			m_InitialExportRect.Height(),
			SWP_NOACTIVATE | SWP_NOZORDER);
	}
}

void CDlg_Conflicts::GetFilterStatus()
{
	if (!m_pSSAMDoc->IsFilterApplied())
	{
		GetDlgItem(IDC_STATIC_FILTERMSG)->SetWindowText("NO FILTER APPLIED");
		m_pConflictList = &(m_pSSAMDoc->GetConflictList());
	} else
	{
		GetDlgItem(IDC_STATIC_FILTERMSG)->SetWindowText("FILTER APPLIED");
		m_pConflictList = &(m_pSSAMDoc->GetFilteredConflictList());
	}
}

