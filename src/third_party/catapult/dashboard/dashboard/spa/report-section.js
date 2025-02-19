/* Copyright 2019 The Chromium Authors. All rights reserved.
   Use of this source code is governed by a BSD-style license that can be
   found in the LICENSE file.
*/
'use strict';

export default class ReportSection extends cp.ElementBase {
  static get template() {
    return Polymer.html`
      <style>
        #tables {
          align-items: center;
          display: flex;
          flex-direction: column;
        }
        report-template {
          background-color: var(--background-color, white);
          overflow: auto;
        }
      </style>

      <report-controls state-path="[[statePath]]">
      </report-controls>

      <cp-loading loading$="[[isLoading]]"></cp-loading>

      <div id="tables">
        <template is="dom-repeat" items="[[tables]]" as="table"
                                  index-as="tableIndex">
          <cp-loading loading$="[[table.isLoading]]"></cp-loading>

          <report-table state-path="[[statePath]].tables.[[tableIndex]]">
          </report-table>

          <template is="dom-if" if="[[table.isEditing]]">
            <cp-dialog>
              <report-template
                  state-path="[[statePath]].tables.[[tableIndex]]"
                  on-save="onSave_">
              </report-template>
            </cp-dialog>
          </template>
        </template>
      </div>
    `;
  }

  ready() {
    super.ready();
    this.scrollIntoView(true);
  }

  async onSave_(event) {
    await this.dispatch('loadReports', this.statePath);
  }

  observeSources_() {
    this.debounce('loadReports', () => {
      this.dispatch('loadReports', this.statePath);
    }, Polymer.Async.timeOut.after(200));
  }
}

ReportSection.State = {
  ...cp.ReportControls.State,
  isLoading: options => false,
  tables: options => [cp.ReportTable.placeholderTable(
      cp.ReportControls.DEFAULT_NAME)],
};

ReportSection.buildState = options => cp.buildState(
    ReportSection.State, options);

ReportSection.properties = {
  ...cp.buildProperties('state', ReportSection.State),
  userEmail: {statePath: 'userEmail'},
};
ReportSection.observers = [
  'observeSources_(source.selectedOptions, minRevision, maxRevision)',
];

ReportSection.actions = {
  restoreState: (statePath, options) => async(dispatch, getState) => {
    dispatch({
      type: ReportSection.reducers.restoreState.name,
      statePath,
      options,
    });
    const state = Polymer.Path.get(getState(), statePath);
    if (state.minRevision === undefined ||
        state.maxRevision === undefined) {
      cp.ReportControls.actions.selectMilestone(
          statePath, state.milestone)(dispatch, getState);
    }
  },

  loadReports: statePath => async(dispatch, getState) => {
    let state = Polymer.Path.get(getState(), statePath);
    if (!state.minRevision || !state.maxRevision) return;

    dispatch({
      type: ReportSection.reducers.requestReports.name,
      statePath,
    });

    const names = state.source.selectedOptions.filter(name =>
      name !== cp.ReportControls.CREATE);
    const requestedReports = new Set(state.source.selectedOptions);
    const revisions = [state.minRevision, state.maxRevision];
    const reportTemplateInfos = await new cp.ReportNamesRequest().response;
    const readers = [];

    for (const name of names) {
      for (const templateInfo of reportTemplateInfos) {
        if (templateInfo.name === name) {
          readers.push(new cp.ReportRequest(
              {...templateInfo, revisions}).reader());
        }
      }
    }

    for await (const {results, errors} of new cp.BatchIterator(readers)) {
      state = Polymer.Path.get(getState(), statePath);
      if (!tr.b.setsEqual(requestedReports, new Set(
          state.source.selectedOptions)) ||
          (state.minRevision !== revisions[0]) ||
          (state.maxRevision !== revisions[1])) {
        return;
      }
      dispatch({
        type: ReportSection.reducers.receiveReports.name,
        statePath,
        reports: results,
      });
    }

    dispatch(Redux.UPDATE(statePath, {isLoading: false}));
  },
};

ReportSection.reducers = {
  restoreState: (state, action, rootState) => {
    if (!action.options) return state;
    const source = {
      ...state.source,
      selectedOptions: action.options.sources,
    };
    return {
      ...state,
      source,
      milestone: parseInt(action.options.milestone ||
        cp.ReportControls.CURRENT_MILESTONE),
      minRevision: action.options.minRevision,
      maxRevision: action.options.maxRevision,
      minRevisionInput: action.options.minRevision,
      maxRevisionInput: action.options.maxRevision,
    };
  },

  requestReports: (state, action, rootState) => {
    const tables = [];
    const tableNames = new Set();
    const selectedNames = state.source.selectedOptions;
    for (const table of state.tables) {
      // Remove tables whose names are unselected.
      if (selectedNames.includes(table.name)) {
        tables.push(table);
        tableNames.add(table.name);
      }
    }
    for (const name of selectedNames) {
      // Add placeholderTables for missing names.
      if (!tableNames.has(name)) {
        if (name === cp.ReportControls.CREATE) {
          tables.push(ReportSection.newTemplate(rootState.userEmail));
        } else {
          tables.push(cp.ReportTable.placeholderTable(name));
        }
      }
    }
    return {...state, isLoading: true, tables};
  },

  receiveReports: (state, {reports}, rootState) => {
    const tables = [...state.tables];
    for (const report of reports) {
      if (!report || !report.report || !report.report.rows) {
        continue;
      }

      // Remove the placeholderTable for this report.
      const placeholderIndex = tables.findIndex(table =>
        table && (table.name === report.name));
      tables.splice(placeholderIndex, 1);

      const rows = report.report.rows.map(
          row => ReportSection.transformReportRow(
              row, state.minRevision, state.maxRevision,
              report.report.statistics));

      // Right-align labelParts.
      const maxLabelParts = tr.b.math.Statistics.max(rows, row =>
        row.labelParts.length);
      for (const {labelParts} of rows) {
        while (labelParts.length < maxLabelParts) {
          labelParts.unshift({
            href: '',
            isFirst: true,
            label: '',
            rowCount: 1,
          });
        }
      }

      // Compute labelPart.isFirst, labelPart.rowCount.
      for (let rowIndex = 1; rowIndex < rows.length; ++rowIndex) {
        for (let partIndex = 0; partIndex < maxLabelParts; ++partIndex) {
          if (rows[rowIndex].labelParts[partIndex].label !==
              rows[rowIndex - 1].labelParts[partIndex].label) {
            continue;
          }
          rows[rowIndex].labelParts[partIndex].isFirst = false;
          let firstRi = rowIndex - 1;
          while (!rows[firstRi].labelParts[partIndex].isFirst) {
            --firstRi;
          }
          ++rows[firstRi].labelParts[partIndex].rowCount;
        }
      }

      tables.push({
        name: report.name,
        id: report.id,
        internal: report.internal,
        canEdit: false,
        isEditing: false,
        rows,
        tooltip: {},
        maxLabelParts,
        owners: (report.owners || []).join(', '),
        statistic: {
          label: 'Statistics',
          query: '',
          options: [
            'avg',
            'std',
            'count',
            'min',
            'max',
            'median',
            'iqr',
            '90%',
            '95%',
            '99%',
          ],
          selectedOptions: report.report.statistics,
          required: true,
        },
      });
    }
    return {...state, tables};
  },
};

ReportSection.newTemplate = userEmail => {
  return {
    isEditing: true,
    name: '',
    owners: userEmail,
    url: '',
    statistics: [],
    rows: [cp.ReportTemplate.newTemplateRow({})],
    statistic: {
      label: 'Statistics',
      query: '',
      options: [
        'avg',
        'std',
        'count',
        'min',
        'max',
        'median',
        'iqr',
        '90%',
        '95%',
        '99%',
      ],
      selectedOptions: ['avg'],
      required: true,
    },
  };
};

function maybeInt(x) {
  const i = parseInt(x);
  return isNaN(i) ? x : i;
}

ReportSection.newStateOptionsFromQueryParams = queryParams => {
  const options = {
    sources: queryParams.getAll('report'),
    milestone: parseInt(queryParams.get('m')) || undefined,
    minRevision: maybeInt(queryParams.get('minRev')) || undefined,
    maxRevision: maybeInt(queryParams.get('maxRev')) || undefined,
  };
  if (options.maxRevision < options.minRevision) {
    [options.maxRevision, options.minRevision] = [
      options.minRevision, options.maxRevision];
  }
  if (options.milestone === undefined &&
      options.minRevision !== undefined &&
      options.maxRevision !== undefined) {
    for (const [milestone, milestoneRevision] of Object.entries(
        cp.ReportControls.CHROMIUM_MILESTONES)) {
      if ((milestoneRevision >= options.minRevision) &&
          ((options.maxRevision === 'latest') ||
            (options.maxRevision >= milestoneRevision))) {
        options.milestone = milestone;
        break;
      }
    }
  }
  return options;
};

ReportSection.getSessionState = state => {
  return {
    sources: state.source.selectedOptions,
    milestone: state.milestone,
  };
};

ReportSection.getRouteParams = state => {
  const routeParams = new URLSearchParams();
  const selectedOptions = state.source.selectedOptions;
  if (state.containsDefaultSection &&
      selectedOptions.length === 1 &&
      selectedOptions[0] === cp.ReportControls.DEFAULT_NAME) {
    return routeParams;
  }
  for (const option of selectedOptions) {
    if (option === cp.ReportControls.CREATE) continue;
    routeParams.append('report', option);
  }
  routeParams.set('minRev', state.minRevision);
  routeParams.set('maxRev', state.maxRevision);
  return routeParams;
};

function chartHref(lineDescriptor) {
  const params = new URLSearchParams({
    measurement: lineDescriptor.measurement,
  });
  for (const suite of lineDescriptor.suites) {
    params.append('suite', suite);
  }
  for (const bot of lineDescriptor.bots) {
    params.append('bot', bot);
  }
  for (const cas of lineDescriptor.cases) {
    params.append('testCase', cas);
  }
  return location.origin + '#' + params;
}

ReportSection.transformReportRow = (
    row, minRevision, maxRevision, statistics) => {
  if (!row.suites) row.suites = row.testSuites;
  if (!row.cases) row.cases = row.testCases;

  const href = chartHref(row);
  const labelParts = row.label.split(':').map(label => {
    return {
      href,
      isFirst: true,
      label,
      rowCount: 1,
    };
  });

  let rowUnit = tr.b.Unit.byJSONName[row.units];
  let conversionFactor = 1;
  if (!rowUnit) {
    rowUnit = tr.b.Unit.byName.unitlessNumber;
    const info = tr.v.LEGACY_UNIT_INFO.get(row.units);
    let improvementDirection = tr.b.ImprovementDirection.DONT_CARE;
    if (info) {
      conversionFactor = info.conversionFactor;
      if (info.defaultImprovementDirection !== undefined) {
        improvementDirection = info.defaultImprovementDirection;
      }
      const unitNameSuffix = tr.b.Unit.nameSuffixForImprovementDirection(
          improvementDirection);
      rowUnit = tr.b.Unit.byName[info.name + unitNameSuffix];
    }
  }
  if (rowUnit.improvementDirection === tr.b.ImprovementDirection.DONT_CARE &&
      row.improvement_direction !== 4) {
    const improvementDirection = (row.improvement_direction === 0) ?
      tr.b.ImprovementDirection.BIGGER_IS_BETTER :
      tr.b.ImprovementDirection.SMALLER_IS_BETTER;
    const unitNameSuffix = tr.b.Unit.nameSuffixForImprovementDirection(
        improvementDirection);
    rowUnit = tr.b.Unit.byName[rowUnit.unitName + unitNameSuffix];
  }

  const scalars = [];
  for (const revision of [minRevision, maxRevision]) {
    for (let statistic of statistics) {
      // IndexedDB can return impartial results if there is no data cached for
      // the requested revision.
      if (!row.data[revision]) {
        scalars.push({}); // insert empty column
        continue;
      }

      if (statistic === 'avg') statistic = 'mean';
      if (statistic === 'std') statistic = 'stddev';

      const unit = (statistic === 'count') ? tr.b.Unit.byName.count :
        rowUnit;
      let unitPrefix;
      if (rowUnit.baseUnit === tr.b.Unit.byName.sizeInBytes) {
        unitPrefix = tr.b.UnitPrefixScale.BINARY.KIBI;
      }
      const running = tr.b.math.RunningStatistics.fromDict(
          row.data[revision].statistics);
      scalars.push({
        unit,
        unitPrefix,
        value: running[statistic],
      });
    }
  }
  for (let statistic of statistics) {
    if (statistic === 'avg') statistic = 'mean';
    if (statistic === 'std') statistic = 'stddev';

    // IndexedDB can return impartial results if there is no data cached for
    // the requested min or max revision.
    if (!row.data[minRevision] || !row.data[maxRevision]) {
      scalars.push({}); // insert empty relative delta
      scalars.push({}); // insert empty absolute delta
      continue;
    }

    const unit = ((statistic === 'count') ? tr.b.Unit.byName.count :
      rowUnit).correspondingDeltaUnit;
    const deltaValue = (
      tr.b.math.RunningStatistics.fromDict(
          row.data[maxRevision].statistics)[statistic] -
      tr.b.math.RunningStatistics.fromDict(
          row.data[minRevision].statistics)[statistic]);
    const suffix = tr.b.Unit.nameSuffixForImprovementDirection(
        unit.improvementDirection);
    scalars.push({
      unit: tr.b.Unit.byName[`normalizedPercentageDelta${suffix}`],
      value: deltaValue / tr.b.math.RunningStatistics.fromDict(
          row.data[minRevision].statistics)[statistic],
    });
    scalars.push({
      unit,
      value: deltaValue,
    });
  }
  const actualDescriptors = (
    row.data[minRevision] || row.data[maxRevision] || {}).descriptors;

  return {
    labelParts,
    scalars,
    label: row.label,
    actualDescriptors,
    ...cp.buildState(cp.TimeseriesDescriptor.State, {
      suite: {
        selectedOptions: row.suites,
        isAggregated: true,
        canAggregate: false,
      },
      measurement: {
        selectedOptions: [row.measurement],
        requireSingle: true,
      },
      bot: {
        selectedOptions: row.bots,
        isAggregated: true,
        canAggregate: false,
      },
      case: {
        selectedOptions: row.cases,
        isAggregated: true,
        canAggregate: false,
      },
    }),
  };
};

cp.ElementBase.register(ReportSection);
