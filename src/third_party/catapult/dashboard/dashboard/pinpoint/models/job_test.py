# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import mock
import sys

from tracing.value.diagnostics import generic_set
from tracing.value.diagnostics import reserved_infos

from dashboard.common import layered_cache
from dashboard.common import utils
from dashboard.models import histogram
from dashboard.pinpoint.models import change
from dashboard.pinpoint.models import errors
from dashboard.pinpoint.models import job
from dashboard.pinpoint import test


_CHROMIUM_URL = 'https://chromium.googlesource.com/chromium/src'


_COMMENT_STARTED = (
    u"""\U0001f4cd Pinpoint job started.
https://testbed.example.com/job/1""")


_COMMENT_COMPLETED_NO_COMPARISON = (
    u"""<b>\U0001f4cd Job complete. See results below.</b>
https://testbed.example.com/job/1""")


_COMMENT_COMPLETED_NO_DIFFERENCES = (
    u"""<b>\U0001f4cd Couldn't reproduce a difference.</b>
https://testbed.example.com/job/1""")


_COMMENT_COMPLETED_WITH_COMMIT = (
    u"""<b>\U0001f4cd Found a significant difference after 1 commit.</b>
https://testbed.example.com/job/1

<b>Subject.</b> by author@chromium.org
https://example.com/repository/+/git_hash
0 \u2192 1.235 (+1.235)

Understanding performance regressions:
  http://g.co/ChromePerformanceRegressions""")

_COMMENT_COMPLETED_WITH_COMMIT_AND_DOCS = (
    u"""<b>\U0001f4cd Found a significant difference after 1 commit.</b>
https://testbed.example.com/job/1

<b>Subject.</b> by author@chromium.org
https://example.com/repository/+/git_hash
0 \u2192 1.235 (+1.235)

Understanding performance regressions:
  http://g.co/ChromePerformanceRegressions

Benchmark doc link:
  http://docs""")

_COMMENT_COMPLETED_WITH_AUTOROLL_COMMIT = (
    u"""<b>\U0001f4cd Found a significant difference after 1 commit.</b>
https://testbed.example.com/job/1

<b>Subject.</b> by chromium-autoroll@skia-public.iam.gserviceaccount.com
https://example.com/repository/+/git_hash
0 \u2192 1.235 (+1.235)

Assigning to sheriff sheriff@bar.com because "Subject." is a roll.

Understanding performance regressions:
  http://g.co/ChromePerformanceRegressions""")


_COMMENT_COMPLETED_WITH_PATCH = (
    u"""<b>\U0001f4cd Found a significant difference after 1 commit.</b>
https://testbed.example.com/job/1

<b>Subject.</b> by author@chromium.org
https://codereview.com/c/672011/2f0d5c7
0 \u2192 1.235 (+1.235)

Understanding performance regressions:
  http://g.co/ChromePerformanceRegressions""")


_COMMENT_COMPLETED_TWO_DIFFERENCES = (
    u"""<b>\U0001f4cd Found significant differences after each of 2 commits.</b>
https://testbed.example.com/job/1

<b>Subject.</b> by author1@chromium.org
https://example.com/repository/+/git_hash_1
0 \u2192 No values

<b>Subject.</b> by author2@chromium.org
https://example.com/repository/+/git_hash_2
No values \u2192 2

Understanding performance regressions:
  http://g.co/ChromePerformanceRegressions""")


_COMMENT_FAILED = (
    u"""\U0001f63f Pinpoint job stopped with an error.
https://testbed.example.com/job/1

Error string""")

_COMMENT_CODE_REVIEW = (
    u"""\U0001f4cd Job complete.

See results at: https://testbed.example.com/job/1""")


class RetryTest(test.TestCase):
  def setUp(self):
    super(RetryTest, self).setUp()

  def testStarted_RecoverableError_BacksOff(self):
    j = job.Job.New((), (), comparison_mode='performance')
    j.Start()
    j.state.Explore = mock.MagicMock(
        side_effect=errors.RecoverableError)
    j._Schedule = mock.MagicMock()
    j.put = mock.MagicMock()
    j.Fail = mock.MagicMock()

    j.Run()
    j.Run()
    j.Run()
    self.assertEqual(j._Schedule.call_args_list[0],
                     mock.call(countdown=job._TASK_INTERVAL * 2))
    self.assertEqual(j._Schedule.call_args_list[1],
                     mock.call(countdown=job._TASK_INTERVAL * 4))
    self.assertEqual(j._Schedule.call_args_list[2],
                     mock.call(countdown=job._TASK_INTERVAL * 8))
    self.assertFalse(j.Fail.called)

    with self.assertRaises(errors.RecoverableError):
      j.Run()
    self.assertTrue(j.Fail.called)

  def testStarted_RecoverableError_Resets(self):
    j = job.Job.New((), (), comparison_mode='performance')
    j.Start()
    j.state.Explore = mock.MagicMock(
        side_effect=errors.RecoverableError)
    j._Schedule = mock.MagicMock()
    j.put = mock.MagicMock()
    j.Fail = mock.MagicMock()

    j.Run()
    j.Run()
    j.Run()
    self.assertEqual(j._Schedule.call_args_list[0],
                     mock.call(countdown=job._TASK_INTERVAL * 2))
    self.assertEqual(j._Schedule.call_args_list[1],
                     mock.call(countdown=job._TASK_INTERVAL * 4))
    self.assertEqual(j._Schedule.call_args_list[2],
                     mock.call(countdown=job._TASK_INTERVAL * 8))
    self.assertFalse(j.Fail.called)

    j.state.Explore = mock.MagicMock()
    j.Run()

    self.assertEqual(0, j.retry_count)


@mock.patch('dashboard.common.utils.ServiceAccountHttp', mock.MagicMock())
class BugCommentTest(test.TestCase):

  def setUp(self):
    super(BugCommentTest, self).setUp()

    self.add_bug_comment = mock.MagicMock()
    self.get_issue = mock.MagicMock()
    patcher = mock.patch('dashboard.services.issue_tracker_service.'
                         'IssueTrackerService')
    issue_tracker_service = patcher.start()
    issue_tracker_service.return_value = mock.MagicMock(
        AddBugComment=self.add_bug_comment, GetIssue=self.get_issue)
    self.addCleanup(patcher.stop)

  def testNoBug(self):
    j = job.Job.New((), ())
    j.Start()
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.assertFalse(self.add_bug_comment.called)

  def testStarted(self):
    j = job.Job.New((), (), bug_id=123456)
    j.Start()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_STARTED, send_email=False)

  def testCompletedNoComparison(self):
    j = job.Job.New((), (), bug_id=123456)
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_NO_COMPARISON)

  def testCompletedNoDifference(self):
    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_NO_DIFFERENCES)

  @mock.patch('dashboard.pinpoint.models.change.commit.Commit.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedWithCommit(self, differences, result_values, commit_as_dict):
    c = change.Change((change.Commit('chromium', 'git_hash'),))
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    commit_as_dict.return_value = {
        'repository': 'chromium',
        'git_hash': 'git_hash',
        'url': 'https://example.com/repository/+/git_hash',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'message': 'Subject.\n\nCommit message.',
    }

    self.get_issue.return_value = {'status': 'Untriaged'}

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_COMMIT,
        status='Assigned', owner='author@chromium.org',
        cc_list=['author@chromium.org'], merge_issue=None)

  @mock.patch('dashboard.pinpoint.models.change.commit.Commit.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedMergeIntoExisting(
      self, differences, result_values, commit_as_dict):
    c = change.Change((change.Commit('chromium', 'git_hash'),))
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    commit_as_dict.return_value = {
        'repository': 'chromium',
        'git_hash': 'git_hash',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'url': 'https://example.com/repository/+/git_hash',
        'message': 'Subject.\n\nCommit message.',
    }

    self.get_issue.return_value = {'status': 'Untriaged', 'id': '111222'}
    layered_cache.SetExternal('commit_hash_git_hash', 111222)

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_COMMIT,
        status='Assigned', owner='author@chromium.org',
        cc_list=[], merge_issue='111222')

  @mock.patch('dashboard.pinpoint.models.change.commit.Commit.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedSkipsMergeWhenDuplicate(
      self, differences, result_values, commit_as_dict):
    c = change.Change((change.Commit('chromium', 'git_hash'),))
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    commit_as_dict.return_value = {
        'repository': 'chromium',
        'git_hash': 'git_hash',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'url': 'https://example.com/repository/+/git_hash',
        'message': 'Subject.\n\nCommit message.',
    }

    def _GetIssue(bug_id):
      if bug_id == 111222:
        return {'status': 'Duplicate', 'id': '111222'}
      else:
        return {'status': 'Untriaged'}

    self.get_issue.side_effect = _GetIssue

    layered_cache.SetExternal('commit_hash_git_hash', 111222)

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_COMMIT,
        status='Assigned', owner='author@chromium.org',
        cc_list=['author@chromium.org'], merge_issue=None)

  @mock.patch('dashboard.pinpoint.models.change.commit.Commit.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedWithInvalidIssue(
      self, differences, result_values, commit_as_dict):
    c = change.Change((change.Commit('chromium', 'git_hash'),))
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    commit_as_dict.return_value = {
        'repository': 'chromium',
        'git_hash': 'git_hash',
        'url': 'https://example.com/repository/+/git_hash',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'message': 'Subject.\n\nCommit message.',
    }

    self.get_issue.return_value = None

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.assertFalse(self.add_bug_comment.called)

  @mock.patch('dashboard.pinpoint.models.change.commit.Commit.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedWithCommitAndDocs(
      self, differences, result_values, commit_as_dict):
    c = change.Change((change.Commit('chromium', 'git_hash'),))
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    commit_as_dict.return_value = {
        'repository': 'chromium',
        'git_hash': 'git_hash',
        'url': 'https://example.com/repository/+/git_hash',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'message': 'Subject.\n\nCommit message.',
    }

    self.get_issue.return_value = {'status': 'Untriaged'}

    j = job.Job.New(
        (), (), bug_id=123456, comparison_mode='performance',
        tags={'test_path': 'master/bot/benchmark'})

    diag_dict = generic_set.GenericSet([[u'Benchmark doc link', u'http://docs']])
    diag = histogram.SparseDiagnostic(
        data=diag_dict.AsDict(), start_revision=1, end_revision=sys.maxint,
        name=reserved_infos.DOCUMENTATION_URLS.name,
        test=utils.TestKey('master/bot/benchmark'))
    diag.put()

    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_COMMIT_AND_DOCS,
        status='Assigned', owner='author@chromium.org',
        cc_list=['author@chromium.org'], merge_issue=None)

  @mock.patch('dashboard.pinpoint.models.change.patch.GerritPatch.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedWithPatch(self, differences, result_values, patch_as_dict):
    commits = (change.Commit('chromium', 'git_hash'),)
    patch = change.GerritPatch('https://codereview.com', 672011, '2f0d5c7')
    c = change.Change(commits, patch)
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    patch_as_dict.return_value = {
        'url': 'https://codereview.com/c/672011/2f0d5c7',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'message': 'Subject.\n\nCommit message.',
        'git_hash': 'abc123'
    }

    self.get_issue.return_value = {'status': 'Untriaged'}

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_PATCH,
        status='Assigned', owner='author@chromium.org',
        cc_list=['author@chromium.org'], merge_issue=None)

  @mock.patch('dashboard.pinpoint.models.change.patch.GerritPatch.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedDoesNotReassign(
      self, differences, result_values, patch_as_dict):
    commits = (change.Commit('chromium', 'git_hash'),)
    patch = change.GerritPatch('https://codereview.com', 672011, '2f0d5c7')
    c = change.Change(commits, patch)
    c = change.Change(commits, patch)
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    patch_as_dict.return_value = {
        'url': 'https://codereview.com/c/672011/2f0d5c7',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'message': 'Subject.\n\nCommit message.',
        'git_hash': 'abc123'
    }

    self.get_issue.return_value = {'status': 'Assigned'}

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_PATCH, owner=None, status=None,
        cc_list=['author@chromium.org'], merge_issue=None)

  @mock.patch('dashboard.pinpoint.models.change.patch.GerritPatch.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedDoesNotReopen(
      self, differences, result_values, patch_as_dict):
    commits = (change.Commit('chromium', 'git_hash'),)
    patch = change.GerritPatch('https://codereview.com', 672011, '2f0d5c7')
    c = change.Change(commits, patch)
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    patch_as_dict.return_value = {
        'url': 'https://codereview.com/c/672011/2f0d5c7',
        'author': 'author@chromium.org',
        'subject': 'Subject.',
        'message': 'Subject.\n\nCommit message.',
        'git_hash': 'abc123'
    }

    self.get_issue.return_value = {'status': 'Fixed'}

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_PATCH, owner=None, status=None,
        cc_list=['author@chromium.org'], merge_issue=None)

  @mock.patch('dashboard.pinpoint.models.change.commit.Commit.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedMultipleDifferences(
      self, differences, result_values, commit_as_dict):
    c1 = change.Change((change.Commit('chromium', 'git_hash_1'),))
    c2 = change.Change((change.Commit('chromium', 'git_hash_2'),))
    differences.return_value = [(None, c1), (None, c2)]
    result_values.side_effect = [0], [], [], [2]
    commit_as_dict.side_effect = (
        {
            'repository': 'chromium',
            'git_hash': 'git_hash_1',
            'url': 'https://example.com/repository/+/git_hash_1',
            'author': 'author1@chromium.org',
            'subject': 'Subject.',
            'message': 'Subject.\n\nCommit message.',
        },
        {
            'repository': 'chromium',
            'git_hash': 'git_hash_2',
            'url': 'https://example.com/repository/+/git_hash_2',
            'author': 'author2@chromium.org',
            'subject': 'Subject.',
            'message': 'Subject.\n\nCommit message.',
        },
    )

    self.get_issue.return_value = {'status': 'Untriaged'}

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_TWO_DIFFERENCES,
        status='Assigned', owner='author2@chromium.org',
        cc_list=['author1@chromium.org', 'author2@chromium.org'],
        merge_issue=None)

  @mock.patch('dashboard.pinpoint.models.change.commit.Commit.AsDict')
  @mock.patch.object(job.job_state.JobState, 'ResultValues')
  @mock.patch.object(job.job_state.JobState, 'Differences')
  def testCompletedWithAutoroll(
      self, differences, result_values, commit_as_dict):
    c = change.Change((change.Commit('chromium', 'git_hash'),))
    differences.return_value = [(None, c)]
    result_values.side_effect = [0], [1.23456]
    commit_as_dict.return_value = {
        'repository': 'chromium',
        'git_hash': 'git_hash',
        'url': 'https://example.com/repository/+/git_hash',
        'author': 'chromium-autoroll@skia-public.iam.gserviceaccount.com',
        'subject': 'Subject.',
        'message': 'Subject.\n\nCommit message.\n\nTBR=sheriff@bar.com',
    }

    self.get_issue.return_value = {'status': 'Untriaged'}

    j = job.Job.New((), (), bug_id=123456, comparison_mode='performance')
    j.put()
    j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(
        123456, _COMMENT_COMPLETED_WITH_AUTOROLL_COMMIT,
        status='Assigned', owner='sheriff@bar.com',
        cc_list=['chromium-autoroll@skia-public.iam.gserviceaccount.com'],
        merge_issue=None)

  @mock.patch.object(job.job_state.JobState, 'ScheduleWork',
                     mock.MagicMock(side_effect=AssertionError('Error string')))
  def testFailed(self):
    j = job.Job.New((), (), bug_id=123456)
    with self.assertRaises(AssertionError):
      j.Run()

    self.ExecuteDeferredTasks('default')

    self.add_bug_comment.assert_called_once_with(123456, _COMMENT_FAILED)

  @mock.patch('dashboard.services.gerrit_service.PostChangeComment')
  def testCompletedUpdatesGerrit(self, post_change_comment):
    j = job.Job.New(
        (), (), gerrit_server='https://review.com', gerrit_change_id='123456')
    j.Run()

    self.ExecuteDeferredTasks('default')

    post_change_comment.assert_called_once_with(
        'https://review.com', '123456', _COMMENT_CODE_REVIEW)
