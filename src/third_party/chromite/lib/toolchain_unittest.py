# -*- coding: utf-8 -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for toolchain."""

from __future__ import print_function

import mock
import os

from chromite.lib import cros_test_lib
from chromite.lib import osutils
from chromite.lib import portage_util
from chromite.lib import sysroot_lib
from chromite.lib import toolchain


BASE_TOOLCHAIN_CONF = """# The root of all evil is money, err, this config.
base-target-name # This will become the base target.

# This toolchain is bonus!
bonus-toolchain {"a setting": "bonus value"}  # Bonus!

"""

ADDITIONAL_TOOLCHAIN_CONF = """# A helpful toolchain related comment.
extra-toolchain  # Unlikely to win any performance tests.

bonus-toolchain {"stable": true}
"""

EXPECTED_TOOLCHAINS = {
    'bonus-toolchain': {
        'sdk': True,
        'crossdev': '',
        'default': False,
        'a setting': 'bonus value',
        'stable': True,
    },
    'extra-toolchain': {'sdk': True, 'crossdev': '', 'default': True},
    'base-target-name': {'sdk': True, 'crossdev': '', 'default': False},
}


class ToolchainTest(cros_test_lib.MockTempDirTestCase):
  """Tests for lib.toolchain."""

  def testArchForToolchain(self):
    """Tests that we correctly parse crossdev's output."""
    rc_mock = cros_test_lib.RunCommandMock()

    noarch = """target=foo
category=bla
"""
    rc_mock.SetDefaultCmdResult(output=noarch)
    with rc_mock:
      self.assertEqual(None, toolchain.GetArchForTarget('fake_target'))

    amd64arch = """arch=amd64
target=foo
"""
    rc_mock.SetDefaultCmdResult(output=amd64arch)
    with rc_mock:
      self.assertEqual('amd64', toolchain.GetArchForTarget('fake_target'))

  @mock.patch('chromite.lib.toolchain.portage_util.FindOverlays')
  def testReadsBoardToolchains(self, find_overlays_mock):
    """Tests that we correctly parse toolchain configs for an overlay stack."""
    # Create some fake overlays and put toolchain confs in a subset of them.
    overlays = [os.path.join(self.tempdir, 'overlay%d' % i) for i in range(3)]
    for overlay in overlays:
      osutils.SafeMakedirs(overlay)
    for overlay, contents in [(overlays[0], BASE_TOOLCHAIN_CONF),
                              (overlays[2], ADDITIONAL_TOOLCHAIN_CONF)]:
      osutils.WriteFile(os.path.join(overlay, 'toolchain.conf'), contents)
    find_overlays_mock.return_value = overlays
    actual_targets = toolchain.GetToolchainsForBoard('board_value')
    self.assertEqual(EXPECTED_TOOLCHAINS, actual_targets)


class ToolchainInfoTest(cros_test_lib.MockTestCase):
  """Tests for the ToolchainInfo class."""

  def setUp(self):
    unused_fields = {'pv': None, 'package': None, 'version_no_rev': None,
                     'rev': None, 'category': None, 'cp': None, 'cpv': None}
    self.gcc_cpv = portage_util.CPV(cpf='sys-devel/gcc-1.2', version='1.2',
                                    **unused_fields)
    self.libc_cpv = portage_util.CPV(cpf='sys-libs/glibc-3.4.5',
                                     version='3.4.5', **unused_fields)
    self.go_cpv = portage_util.CPV(cpf='dev-lang/go-6.7-r8', version='6.7-r8',
                                   **unused_fields)

    self.matching_toolchain = toolchain.ToolchainInfo('tc', 'tc')
    self.not_matching_toolchain = toolchain.ToolchainInfo('tc', 'dtc')

  def testVersion(self):
    """Test the version fetching functionality."""
    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.gcc_cpv)
    self.assertEqual('1.2', self.matching_toolchain.gcc_version)

    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.libc_cpv)
    self.assertEqual('3.4.5', self.matching_toolchain.libc_version)

    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.go_cpv)
    self.assertEqual('6.7-r8', self.matching_toolchain.go_version)

  def testCpv(self):
    """Test the CPV version functionality."""
    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.gcc_cpv)
    self.assertEqual(self.gcc_cpv.cpf, self.matching_toolchain.gcc_cpf)

    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.libc_cpv)
    self.assertEqual(self.libc_cpv.cpf, self.matching_toolchain.libc_cpf)

    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.go_cpv)
    self.assertEqual(self.go_cpv.cpf, self.matching_toolchain.go_cpf)

  def testCP(self):
    """Test the GetCP method."""
    # pylint: disable=protected-access
    # Use wrong CPV instances to make sure it's not using them since _GetCP
    # is the "base case" for fetching the CPV objects.
    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.go_cpv)
    self.PatchObject(self.not_matching_toolchain, '_GetCPVObj',
                     return_value=self.go_cpv)
    self.assertEqual('sys-devel/gcc', self.matching_toolchain._GetCP('gcc'))
    self.assertEqual('cross-tc/gcc', self.not_matching_toolchain._GetCP('gcc'))

    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.go_cpv)
    self.PatchObject(self.not_matching_toolchain, '_GetCPVObj',
                     return_value=self.go_cpv)
    self.assertEqual('sys-libs/glibc', self.matching_toolchain._GetCP('glibc'))
    self.assertEqual('cross-tc/glibc',
                     self.not_matching_toolchain._GetCP('glibc'))

    self.PatchObject(self.matching_toolchain, '_GetCPVObj',
                     return_value=self.gcc_cpv)
    self.PatchObject(self.not_matching_toolchain, '_GetCPVObj',
                     return_value=self.gcc_cpv)
    self.assertEqual('dev-lang/go', self.matching_toolchain._GetCP('go'))
    self.assertEqual('cross-tc/go', self.not_matching_toolchain._GetCP('go'))


class ToolchainInstallerTest(cros_test_lib.TempDirTestCase):
  """Tests for the toolchain installer class."""

  def setUp(self):
    # Setup the temp filesystem matching the expected layout.
    D = cros_test_lib.Directory
    filesystem = (
        D('build', (
            D('board', (
                D('etc', (
                    D('portage', (
                        D('profile', ('package.provided',)),
                    )),
                    # 'make.conf.board_setup',
                )),
                D('var', (
                    D('cache', (
                        D('edb', ('chromeos', 'chromeos.lock',)),
                    )),
                    D('lib', (
                        D('portage', (
                            D('pkgs', ()),
                        )),
                    )),
                )),
            )),
        )),
    )
    cros_test_lib.CreateOnDiskHierarchy(self.tempdir, filesystem)
    self.sysroot = sysroot_lib.Sysroot(os.path.join(self.tempdir,
                                                    'build/board'))

    # Build out the testing CPV objects.
    unused_fields = {'pv': None, 'package': None, 'version_no_rev': None,
                     'rev': None, 'category': None, 'cp': None, 'cpv': None}
    self.gcc_cpv = portage_util.CPV(cpf='sys-devel/gcc-1.2', version='1.2',
                                    **unused_fields)
    self.libc_cpv = portage_util.CPV(cpf='sys-libs/glibc-3.4.5',
                                     version='3.4.5', **unused_fields)
    self.go_cpv = portage_util.CPV(cpf='dev-lang/go-6.7-r8', version='6.7-r8',
                                   **unused_fields)
    self.rpcsvc_cpv = portage_util.CPV(cpf='net-libs/rpcsvc-proto-9.10',
                                       version='9.10', **unused_fields)

    # pylint: disable=protected-access
    self.go_toolchain = toolchain.ToolchainInfo('tc', 'tc')
    self.go_toolchain._cpvs = {'gcc': self.gcc_cpv,
                               'glibc': self.libc_cpv,
                               'go': self.go_cpv,
                               'rpcsvc': self.rpcsvc_cpv}

    self.no_go_toolchain = toolchain.ToolchainInfo('tc', 'tc')
    self.no_go_toolchain._cpvs = {'gcc': self.gcc_cpv,
                                  'glibc': self.libc_cpv,
                                  'go': None,
                                  'rpcsvc': self.rpcsvc_cpv}

    self.different_toolchain = toolchain.ToolchainInfo('nottc', 'tc')
    self.different_toolchain._cpvs = {'gcc': self.gcc_cpv,
                                      'glibc': self.libc_cpv,
                                      'go': self.go_cpv,
                                      'rpcsvc': None}

    pkgdir = os.path.join(self.tempdir, 'var/lib/portage/pkgs')
    self.updater = toolchain.ToolchainInstaller(False, True, 'tc', pkgdir)


  def testUpdateProvided(self):
    """Test the updates to the package.provided file."""
    path = os.path.join(self.sysroot.path,
                        'etc/portage/profile/package.provided')

    # pylint: disable=protected-access
    # All 3 packages.
    self.updater._UpdateProvided(self.sysroot, self.go_toolchain)
    expected = ['sys-devel/gcc-1.2',
                'sys-libs/glibc-3.4.5',
                'dev-lang/go-6.7-r8',
                'net-libs/rpcsvc-proto-9.10']

    for line in osutils.ReadFile(path).splitlines():
      self.assertIn(line, expected)
      expected.remove(line)

    self.assertEqual([], expected)

    # No go package.
    self.updater._UpdateProvided(self.sysroot, self.no_go_toolchain)
    expected = ['sys-devel/gcc-1.2',
                'sys-libs/glibc-3.4.5',
                'net-libs/rpcsvc-proto-9.10']

    for line in osutils.ReadFile(path).splitlines():
      self.assertIn(line, expected)
      expected.remove(line)

    self.assertEqual([], expected)

    # Different toolchain.
    self.updater._UpdateProvided(self.sysroot, self.different_toolchain)
    expected = ['sys-devel/gcc-1.2',
                'sys-libs/glibc-3.4.5',
                'dev-lang/go-6.7-r8']

    for line in osutils.ReadFile(path).splitlines():
      self.assertIn(line, expected)
      expected.remove(line)

    self.assertEqual([], expected)

  def testWriteConfig(self):
    """Test the sysroot configs are updated correctly."""
    # pylint: disable=protected-access
    self.updater._WriteConfigs(self.sysroot, self.go_toolchain)
    self.assertEqual('3.4.5', self.sysroot.GetCachedField('LIBC_VERSION'))
