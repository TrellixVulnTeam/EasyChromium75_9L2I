// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef XFA_FXFA_LAYOUT_CXFA_LAYOUTPAGEMGR_H_
#define XFA_FXFA_LAYOUT_CXFA_LAYOUTPAGEMGR_H_

#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <vector>

#include "third_party/base/optional.h"
#include "xfa/fxfa/layout/cxfa_itemlayoutprocessor.h"

class CXFA_LayoutItem;
class CXFA_Node;
class CXFA_ViewRecord;

class CXFA_LayoutPageMgr {
 public:
  struct BreakData {
    CXFA_Node* pLeader;
    CXFA_Node* pTrailer;
    bool bCreatePage;
  };

  struct OverflowData {
    CXFA_Node* pLeader;
    CXFA_Node* pTrailer;
  };

  explicit CXFA_LayoutPageMgr(CXFA_LayoutProcessor* pLayoutProcessor);
  ~CXFA_LayoutPageMgr();

  bool InitLayoutPage(CXFA_Node* pFormNode);
  bool PrepareFirstPage(CXFA_Node* pRootSubform);
  float GetAvailHeight();
  bool GetNextAvailContentHeight(float fChildHeight);
  void SubmitContentItem(CXFA_ContentLayoutItem* pContentLayoutItem,
                         CXFA_ItemLayoutProcessor::Result eStatus);
  void FinishPaginatedPageSets();
  void SyncLayoutData();
  int32_t GetPageCount() const;
  CXFA_ViewLayoutItem* GetPage(int32_t index) const;
  int32_t GetPageIndex(const CXFA_ViewLayoutItem* pPage) const;
  inline CXFA_ViewLayoutItem* GetRootLayoutItem() const {
    return m_pPageSetLayoutItemRoot;
  }
  Optional<BreakData> ProcessBreakBefore(const CXFA_Node* pBreakNode);
  Optional<BreakData> ProcessBreakAfter(const CXFA_Node* pBreakNode);
  Optional<OverflowData> ProcessOverflow(CXFA_Node* pFormNode,
                                         bool bCreatePage);
  CXFA_Node* QueryOverflow(CXFA_Node* pFormNode);
  CXFA_Node* ProcessBookendLeader(const CXFA_Node* pBookendNode);
  CXFA_Node* ProcessBookendTrailer(const CXFA_Node* pBookendNode);

 private:
  using RecordList = std::list<std::unique_ptr<CXFA_ViewRecord>>;

  bool AppendNewPage(bool bFirstTemPage);
  void ReorderPendingLayoutRecordToTail(CXFA_ViewRecord* pNewRecord,
                                        CXFA_ViewRecord* pPrevRecord);
  void RemoveLayoutRecord(CXFA_ViewRecord* pNewRecord,
                          CXFA_ViewRecord* pPrevRecord);
  bool HasCurrentViewRecord() const {
    return m_CurrentViewRecordIter != m_ProposedViewRecords.end();
  }
  CXFA_ViewRecord* GetCurrentViewRecord() {
    return m_CurrentViewRecordIter->get();
  }
  const CXFA_ViewRecord* GetCurrentViewRecord() const {
    return m_CurrentViewRecordIter->get();
  }
  void ResetToFirstViewRecord() {
    m_CurrentViewRecordIter = m_ProposedViewRecords.begin();
  }
  RecordList::iterator GetTailPosition() {
    auto iter = m_ProposedViewRecords.end();
    return !m_ProposedViewRecords.empty() ? std::prev(iter) : iter;
  }
  CXFA_ViewRecord* AppendNewRecord(std::unique_ptr<CXFA_ViewRecord> pNewRecord);
  CXFA_ViewRecord* CreateViewRecord(CXFA_Node* pPageNode, bool bCreateNew);
  CXFA_ViewRecord* CreateViewRecordSimple();
  void AddPageAreaLayoutItem(CXFA_ViewRecord* pNewRecord,
                             CXFA_Node* pNewPageArea);
  void AddContentAreaLayoutItem(CXFA_ViewRecord* pNewRecord,
                                CXFA_Node* pContentArea);
  bool RunBreak(XFA_Element eBreakType,
                XFA_AttributeValue eTargetType,
                CXFA_Node* pTarget,
                bool bStartNew);
  bool ShouldGetNextPageArea(CXFA_Node* pTarget, bool bStartNew) const;
  bool BreakOverflow(const CXFA_Node* pOverflowNode,
                     bool bCreatePage,
                     CXFA_Node** pLeaderTemplate,
                     CXFA_Node** pTrailerTemplate);
  CXFA_Node* ProcessBookendLeaderOrTrailer(const CXFA_Node* pBookendNode,
                                           bool bLeader);
  CXFA_Node* ResolveBookendLeaderOrTrailer(const CXFA_Node* pBookendNode,
                                           bool bLeader);
  Optional<BreakData> ProcessBreakBeforeOrAfter(const CXFA_Node* pBreakNode,
                                                bool bBefore);
  BreakData ExecuteBreakBeforeOrAfter(const CXFA_Node* pCurNode, bool bBefore);

  int32_t CreateMinPageRecord(CXFA_Node* pPageArea,
                              bool bTargetPageArea,
                              bool bCreateLast);
  void CreateMinPageSetRecord(CXFA_Node* pPageSet, bool bCreateAll);
  void CreateNextMinRecord(CXFA_Node* pRecordNode);
  bool FindPageAreaFromPageSet(CXFA_Node* pPageSet,
                               CXFA_Node* pStartChild,
                               CXFA_Node* pTargetPageArea,
                               CXFA_Node* pTargetContentArea,
                               bool bNewPage,
                               bool bQuery);
  bool FindPageAreaFromPageSet_Ordered(CXFA_Node* pPageSet,
                                       CXFA_Node* pStartChild,
                                       CXFA_Node* pTargetPageArea,
                                       CXFA_Node* pTargetContentArea,
                                       bool bNewPage,
                                       bool bQuery);
  bool FindPageAreaFromPageSet_SimplexDuplex(
      CXFA_Node* pPageSet,
      CXFA_Node* pStartChild,
      CXFA_Node* pTargetPageArea,
      CXFA_Node* pTargetContentArea,
      bool bNewPage,
      bool bQuery,
      XFA_AttributeValue ePreferredPosition);
  bool MatchPageAreaOddOrEven(CXFA_Node* pPageArea);
  CXFA_Node* GetNextAvailPageArea(CXFA_Node* pTargetPageArea,
                                  CXFA_Node* pTargetContentArea,
                                  bool bNewPage,
                                  bool bQuery);
  bool GetNextContentArea(CXFA_Node* pTargetContentArea);
  void InitPageSetMap();
  void ProcessLastPageSet();
  bool IsPageSetRootOrderedOccurrence() const {
    return m_ePageSetMode == XFA_AttributeValue::OrderedOccurrence;
  }
  void ClearData();
  void MergePageSetContents();
  void LayoutPageSetContents();
  void PrepareLayout();
  void SaveLayoutItem(CXFA_LayoutItem* pParentLayoutItem);
  void ProcessSimplexOrDuplexPageSets(CXFA_ViewLayoutItem* pPageSetLayoutItem,
                                      bool bIsSimplex);

  CXFA_LayoutProcessor* m_pLayoutProcessor;
  CXFA_Node* m_pTemplatePageSetRoot;
  CXFA_ViewLayoutItem* m_pPageSetLayoutItemRoot;
  CXFA_ViewLayoutItem* m_pPageSetCurRoot;
  RecordList m_ProposedViewRecords;
  RecordList::iterator m_CurrentViewRecordIter;
  CXFA_Node* m_pCurPageArea;
  int32_t m_nAvailPages;
  int32_t m_nCurPageCount;
  XFA_AttributeValue m_ePageSetMode;
  bool m_bCreateOverFlowPage;
  std::map<CXFA_Node*, int32_t> m_pPageSetMap;
  std::vector<CXFA_ViewLayoutItem*> m_PageArray;
};

#endif  // XFA_FXFA_LAYOUT_CXFA_LAYOUTPAGEMGR_H_
