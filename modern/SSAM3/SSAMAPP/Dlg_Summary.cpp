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
// Dlg_Summary.cpp : implementation file
//

#include "stdafx.h"
#include "SSAMAPP.h"
#include "Dlg_Summary.h"
#include "afxdialogex.h"
#include "Summary.h"

// CDlg_Summary dialog

IMPLEMENT_DYNAMIC(CDlg_Summary, CDialog)

CDlg_Summary::CDlg_Summary(CWnd* pParent /*=NULL*/)
	: CDlg_Tab(pParent)
	, m_InitialClientSize(0, 0)
{

}

CDlg_Summary::CDlg_Summary(SP_SSAMDoc pSSAMDoc, CWnd* pParent)
	: CDlg_Tab(pParent)
	, m_pSSAMDoc (pSSAMDoc)
	, m_InitialClientSize(0, 0)
{

}

CDlg_Summary::~CDlg_Summary()
{
}

void CDlg_Summary::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_MEASURES, m_ListCtrl_Measures);
	DDX_Control(pDX, IDC_LIST_CONFLICTS, m_ListCtrl_Conflicts);
}

BOOL CDlg_Summary::OnInitDialog()
{
	CDialog::OnInitDialog();
	GetFilterStatus();
	// add the headers
	m_ListCtrl_Measures.SetExtendedStyle(m_ListCtrl_Measures.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	for (int i = 0; i < Summary::NUM_SUMMARY_LABELS; i++)
	{
		m_ListCtrl_Measures.InsertColumn(i, _T(Summary::SUMMARY_LABEL[i].c_str()), LVCFMT_LEFT, 90, -1);
	}
	
	m_ListCtrl_Conflicts.SetExtendedStyle(m_ListCtrl_Conflicts.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	m_ListCtrl_Conflicts.InsertColumn(0, _T("Summary Group"), LVCFMT_LEFT, 90, -1);
	m_ListCtrl_Conflicts.InsertColumn(1, _T("Total"), LVCFMT_LEFT, 90, -1);
	for(int i = 0; i < Conflict::NUM_CONFLICT_TYPES; i++)
	{
		m_ListCtrl_Conflicts.InsertColumn(i+2, _T(Conflict::CONFLICT_TYPE_LABEL[i].c_str()), LVCFMT_LEFT, 90, -1);
	}

	CRect clientRect;
	GetClientRect(&clientRect);
	m_InitialClientSize = clientRect.Size();

	m_ListCtrl_Measures.GetWindowRect(&m_InitialMeasuresRect);
	ScreenToClient(&m_InitialMeasuresRect);

	m_ListCtrl_Conflicts.GetWindowRect(&m_InitialConflictsRect);
	ScreenToClient(&m_InitialConflictsRect);

	CWnd* pExportButton = GetDlgItem(IDC_BUTTON_EXPORT);
	if (pExportButton != NULL)
	{
		pExportButton->GetWindowRect(&m_InitialExportRect);
		ScreenToClient(&m_InitialExportRect);
	}
	return TRUE;
}

bool CDlg_Summary::SaveData()
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

void CDlg_Summary::SetData()
{
	GetFilterStatus();
	ListRedrawGuard measuresRedrawGuard(m_ListCtrl_Measures);
	ListRedrawGuard conflictsRedrawGuard(m_ListCtrl_Conflicts);
	m_ListCtrl_Measures.DeleteAllItems();
	m_ListCtrl_Conflicts.DeleteAllItems();

	int iRow = 0, iCRow = 0;

	char tmpStr[1000];
	for (std::list<SP_Summary>::iterator it = m_pSummaries->begin();
		it != m_pSummaries->end(); ++it)
	{
		std::string sumGroup((*it)->GetTrjFile());
		
		// insert items in measure listctrl
		// Insert a blank row
		InsertBlankRow(&m_ListCtrl_Measures, iRow++, Summary::NUM_SUMMARY_LABELS);

		// repeat the measure labels for each file summary
		int iCol = 0;
		int nIdx = m_ListCtrl_Measures.InsertItem(iRow++, _T(Summary::SUMMARY_LABEL[iCol].c_str()));
		for (iCol = 1; iCol < Summary::NUM_SUMMARY_LABELS; iCol++)
		{
			m_ListCtrl_Measures.SetItemText(nIdx, iCol, _T(Summary::SUMMARY_LABEL[iCol].c_str()));
		}

		// insert measures of the summary
		for (int m = 0; m < Conflict::NUM_MEASURES; ++m)
		{
			if (!Conflict::SUM_MEASURES[m])
				continue;
			
			int nIdx = m_ListCtrl_Measures.InsertItem(iRow++, _T(sumGroup.c_str()));
			
			int iCol = 1;
			m_ListCtrl_Measures.SetItemText(nIdx, iCol++, _T(Conflict::MEASURE_LABEL[m].c_str()));

			
			sprintf(tmpStr, "%6.2f", (*it)->GetMinVals()[m]);
			m_ListCtrl_Measures.SetItemText(nIdx, iCol++, _T(tmpStr));

			sprintf(tmpStr, "%6.2f", (*it)->GetMaxVals()[m]);
			m_ListCtrl_Measures.SetItemText(nIdx, iCol++, _T(tmpStr));

			sprintf(tmpStr, "%6.2f", (*it)->GetMeanVals()[m]);
			m_ListCtrl_Measures.SetItemText(nIdx, iCol++, _T(tmpStr));

			sprintf(tmpStr, "%6.2f", (*it)->GetVarVals()[m]);
			m_ListCtrl_Measures.SetItemText(nIdx, iCol++, _T(tmpStr));
		}
		
		// insert items in conflict listctrl
		int nCIdx = m_ListCtrl_Conflicts.InsertItem(iCRow++, _T(sumGroup.c_str()));
		
		sprintf(tmpStr, "%d", (*it)->GetConflictTypeCounts()[Conflict::NUM_CONFLICT_TYPES]);
		int iCCol = 1;
		m_ListCtrl_Conflicts.SetItemText(nCIdx, iCCol++, _T(tmpStr));
		for (int i = 0; i < Conflict::NUM_CONFLICT_TYPES; i++)
		{
			sprintf(tmpStr, "%d", (*it)->GetConflictTypeCounts()[i]);
			m_ListCtrl_Conflicts.SetItemText(nCIdx, iCCol++, _T(tmpStr));
		}
	}

	AutoSizeColumns();
}

void CDlg_Summary::InsertBlankRow(CListCtrl* pListCtrl, int iRow, int nCols)
{
	int nIdx = pListCtrl->InsertItem(iRow, _T(""));
	for (int i = 1; i < nCols; i++)
	{
		pListCtrl->SetItemText(nIdx, i, _T(""));
	}
}

void CDlg_Summary::AutoSizeColumns()
{
	CHeaderCtrl* pMeasureHeader = m_ListCtrl_Measures.GetHeaderCtrl();
	if (pMeasureHeader != NULL)
	{
		for (int i = 0; i < pMeasureHeader->GetItemCount(); ++i)
		{
			m_ListCtrl_Measures.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
		}
	}

	CHeaderCtrl* pConflictHeader = m_ListCtrl_Conflicts.GetHeaderCtrl();
	if (pConflictHeader != NULL)
	{
		for (int i = 0; i < pConflictHeader->GetItemCount(); ++i)
		{
			m_ListCtrl_Conflicts.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
		}
	}
}

BEGIN_MESSAGE_MAP(CDlg_Summary, CDialog)
	ON_BN_CLICKED(IDC_BUTTON_EXPORT, &CDlg_Summary::OnBnClickedButtonExport)
	ON_WM_SIZE()
END_MESSAGE_MAP()


// CDlg_Summary message handlers


void CDlg_Summary::OnBnClickedButtonExport()
{
	std::string sumFileName(m_pSSAMDoc->GetCsvName());
	sumFileName = sumFileName.substr(0, sumFileName.length() - 4) + "_summary.csv";
	CFileDialog fdlg (FALSE, "*.csv", sumFileName.c_str(),OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT |OFN_ENABLESIZING | OFN_EXPLORER,
			"CSV (Comma delimited) (*.csv)|*.csv|",NULL,0,true);	

	if(fdlg.DoModal()==IDOK)
	{
		CString path = fdlg.GetPathName  ();
		std::ofstream csvFile(path);

		// add the headers
		for (int i = 0; i < Summary::NUM_SUMMARY_LABELS; i++)
		{
			csvFile << Summary::SUMMARY_LABEL[i] << ",";
		}
		csvFile << std::endl;
	
		for (std::list<SP_Summary>::iterator it = m_pSummaries->begin();
			it != m_pSummaries->end(); ++it)
		{
			std::string sumGroup((*it)->GetTrjFile());
		
			// write items 
			// Insert a blank row
			csvFile << std::endl;

			// repeat the measure labels for each file summary
			for (int i = 0; i < Summary::NUM_SUMMARY_LABELS; i++)
			{
				csvFile << Summary::SUMMARY_LABEL[i] << ",";
			}
			csvFile << std::endl;

			// insert measures of the summary
			for (int m = 0; m < Conflict::NUM_MEASURES; ++m)
			{
				if (!Conflict::SUM_MEASURES[m])
					continue;
			
				csvFile << sumGroup << ",";
				csvFile << Conflict::MEASURE_LABEL[m] << ",";
				csvFile << (*it)->GetMinVals()[m] << ",";
				csvFile << (*it)->GetMaxVals()[m] << ",";
				csvFile << (*it)->GetMeanVals()[m] << ",";
				csvFile << (*it)->GetVarVals()[m] << std::endl;
			}
		}
		
		csvFile << std::endl;
		csvFile << std::endl;

		csvFile << "Summary Group , Total, ";
		for(int i = 0; i < Conflict::NUM_CONFLICT_TYPES; i++)
		{
			csvFile << Conflict::CONFLICT_TYPE_LABEL[i] << ",";
		}
		csvFile << std::endl;
		
		for (std::list<SP_Summary>::iterator it = m_pSummaries->begin();
			it != m_pSummaries->end(); ++it)
		{
			std::string sumGroup((*it)->GetTrjFile());
			csvFile << sumGroup << ",";
			csvFile << (*it)->GetConflictTypeCounts()[Conflict::NUM_CONFLICT_TYPES] << ",";

			for (int i = 0; i < Conflict::NUM_CONFLICT_TYPES; i++)
			{
				csvFile << (*it)->GetConflictTypeCounts()[i] << ",";
			}
			csvFile << std::endl;
		}

		csvFile.close();
	}
}

void CDlg_Summary::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);

	if (m_InitialClientSize.cx == 0
		|| !::IsWindow(m_ListCtrl_Measures.GetSafeHwnd())
		|| !::IsWindow(m_ListCtrl_Conflicts.GetSafeHwnd()))
	{
		return;
	}

	const int dx = cx - m_InitialClientSize.cx;
	const int dy = cy - m_InitialClientSize.cy;
	int measuresWidth = m_InitialMeasuresRect.Width() + dx;
	int conflictsWidth = m_InitialConflictsRect.Width() + dx;
	int conflictsHeight = m_InitialConflictsRect.Height() + dy;

	if (measuresWidth < 0) measuresWidth = 0;
	if (conflictsWidth < 0) conflictsWidth = 0;
	if (conflictsHeight < 0) conflictsHeight = 0;

	m_ListCtrl_Measures.SetWindowPos(NULL,
		m_InitialMeasuresRect.left,
		m_InitialMeasuresRect.top,
		measuresWidth,
		m_InitialMeasuresRect.Height(),
		SWP_NOACTIVATE | SWP_NOZORDER);

	m_ListCtrl_Conflicts.SetWindowPos(NULL,
		m_InitialConflictsRect.left,
		m_InitialConflictsRect.top,
		conflictsWidth,
		conflictsHeight,
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

void CDlg_Summary::GetFilterStatus()
{
	if (!m_pSSAMDoc->IsFilterApplied())
	{
		GetDlgItem(IDC_STATIC_FILTERMSG)->SetWindowText("NO FILTER APPLIED");
		m_pSummaries = &(m_pSSAMDoc->GetSummaries());
	} else
	{
		GetDlgItem(IDC_STATIC_FILTERMSG)->SetWindowText("FILTER APPLIED");
		m_pSummaries = &(m_pSSAMDoc->GetFilteredSummaries());
	}
}

