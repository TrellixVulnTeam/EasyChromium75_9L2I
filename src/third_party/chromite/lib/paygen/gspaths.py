# -*- coding: utf-8 -*-
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Centralize knowledge about how to create standardized Google Storage paths.

This includes definitions for various build flags:

  LOCK - means that payload processing is in progress on the host which
         owns the locks. Locks have a timeout associated with them in
         case of error, but are not 100% atomic when a lock is timing out.

  Example file paths:
    gs://chromeos-releases/blah-channel/board-name/1.2.3/payloads/LOCK_flag
"""

from __future__ import print_function

import hashlib
import os
import random
import re

from chromite.lib import cros_logging as logging
from chromite.lib.paygen import utils


class Build(utils.RestrictedAttrDict):
  """Define a ChromeOS Build.

  The order of attributes in self._slots dictates the order attributes
  are printed in by __str__ method of super class.  Keep the attributes
  that are more helpful in identifying this build earlier in the list,
  because this string ends up cut off in email subjects.

  Fields:
    board: The board of the image "x86-mario", etc.
    bucket: The bucket of the image. "chromeos-releases" as default.
    channel: The channel of the image "stable-channel", "nplusone", etc.
    uri: The URI of the build directory.
    version: The version of the image. "0.14.23.2", "3401.0.0", etc.
  """
  _slots = ('board', 'version', 'channel', 'bucket', 'uri')
  _name = 'Build definition'

  def __init__(self, *args, **kwargs):
    super(Build, self).__init__(*args, **kwargs)

  @staticmethod
  def BuildValuesFromUri(uri_re, uri):
    """Builds a dictionary from a URI using a regular expression.

    In addition it remove the 'board', 'version', 'channel', and 'bucket' keys
    and replaces them with on Build object.

    Args:
      uri_re: A regular expression to match with the given URI.
      uri: The URI to match against the regular expression.

    Returns:
      A dictionary containing all the necessary files or None if it could not
      match the URI against the regular expression.
    """
    m = re.match(uri_re, uri)
    if not m:
      return None
    values = m.groupdict()

    # Replace Build values with Build object.
    build_keys = ('bucket', 'channel', 'board', 'version')
    values['build'] = Build({key: values[key] for key in build_keys})
    for key in build_keys:
      del values[key]

    return values


class Image(utils.RestrictedAttrDict):
  """Define a ChromeOS Image.

  Fields:
    build: An instance of gspaths.Build that defines the build.
    image_channel: Sometimes an image has a different channel than the build
                   directory it's in. (ie: nplusone). None otherwise.
    image_version: Sometimes an image has a different version than the build
                   directory it's in. (ie: nplusone). None otherwise.
    image_type: The type of the image. Currently, "recovery" or "base" types
                are supported.
    key: The key the image was signed with. "premp", "mp", "mp-v2"
         This is not the board specific key name, but the general value used
         in image/payload names.
    uri: The URI of the image. This URI can be any format understood by
         urilib.
  """
  _name = 'Image definition'
  _slots = ('build', 'image_type', 'key', 'image_channel', 'image_version',
            'uri')
  DEFAULT_IMAGE_TYPE = 'recovery'

  def __init__(self, *args, **kwargs):
    super(Image, self).__init__(*args, **kwargs)

    # If these match defaults, set to None.
    if self.build:
      self._clear_if_default('image_channel', self.build.channel)
      self._clear_if_default('image_version', self.build.version)

    # Force a default image_type if unspecified.
    if not self.image_type:
      self.image_type = Image.DEFAULT_IMAGE_TYPE

  def __str__(self):
    if self.uri:
      return self.uri.split('/')[-1]
    else:
      return ('Image: %s:%s/%s%s/%s%s/%s/%s (no uri)' %
              (self.build.bucket, self.build.board,
               self.build.channel,
               '(%s)' % self.image_channel if self.image_channel else '',
               self.build.version,
               '(%s)' % self.image_version if self.image_version else '',
               self.image_type, self.key))


class DLCImage(Image):
  """Define a ChromeOS DLC Image.

  Fields:
    dlc_id: ID of a DLC module image.
    dlc_package: Package name of the DLC module.
    dlc_image: File name of a DLC module image.
  """
  _name = 'DLC Image definition'
  _slots = Image._slots + ('dlc_id', 'dlc_package', 'dlc_image')

  def __init__(self, *args, **kwargs):
    super(DLCImage, self).__init__(*args, **kwargs)

  def __str__(self):
    if self.uri:
      return self.uri.split('/')[-1]
    else:
      return '%s %s/%s/%s' % (super(DLCImage, self).__str__(),
                              self.dlc_id,
                              self.dlc_package,
                              self.dlc_image)


class UnsignedImageArchive(utils.RestrictedAttrDict):
  """Define a unsigned ChromeOS image archive.

  Fields:
    bucket: The bucket of the image. "chromeos-releases" as default.
    channel: The channel of the image "stable-channel", "nplusone", etc.
    board: The board of the image "x86-mario", etc.
    version: The version of the image. "0.14.23.2", "3401.0.0", etc.
    milestone: the most recent branch corresponding to the version; "R19" etc
    image_type: "test", "recovery" or "base"
    uri: The URI of the image. This URI can be any format understood by
         urilib.
  """
  _name = 'Unsigned image archive definition'
  _slots = ('build', 'milestone', 'image_type', 'uri')

  def __str__(self):
    if self.uri:
      return '%s' % self.uri.split('/')[-1]
    else:
      return ('Unsigned image archive: %s:%s/%s/%s-%s/%s (no uri)' %
              (self.build.bucket, self.build.board, self.build.channel,
               self.milestone, self.build.version, self.image_type))


class Payload(utils.RestrictedAttrDict):
  """Define a ChromeOS Payload.

  Fields:
    tgt_image: A representation of image the payload updates to, either
               Image or UnsignedImageArchive.
    src_image: A representation of image it updates from. None for
               Full updates, or the same type as tgt_image otherwise.
    build: A build if it is supposed to be different than the tgt_image's
           build.
    uri: The URI of the payload. This can be any format understood by urilib.
    exists: A boolean. If true, artifacts for this build already exist.
  """
  _name = 'Payload definition'
  _slots = ('tgt_image', 'src_image', 'build', 'uri', 'exists')

  def __init__(self, exists=False, *args, **kwargs):
    kwargs.update(exists=exists)
    super(Payload, self).__init__(*args, **kwargs)

    # If there was no build passed, set the target image's build as the default.
    if not self.build and self.tgt_image.build:
      self.build = Build(self.tgt_image.build)

  def __str__(self):
    if self.uri:
      return self.uri.split('/')[-1]
    else:
      return '%s -> %s (no uri)' % (self.src_image or 'any', self.tgt_image)


class ChromeosReleases(object):
  """Name space class for static methods for URIs in chromeos-releases."""

  BUCKET = 'chromeos-releases'

  TEST_BUCKET = 'chromeos-releases-test'

  # Build flags
  LOCK = 'LOCK'

  FLAGS = (LOCK,)

  UNSIGNED_IMAGE_TYPES = ('test', 'recovery', 'base')

  @staticmethod
  def BuildUri(build):
    """Creates the gspath for a given build.

    Args:
      build: An instance of gspaths.Build that defines the build.

    Returns:
      The url for the specified build artifacts. Should be of the form:
      gs://chromeos-releases/blah-channel/board-name/1.2.3
    """
    return 'gs://%s/%s/%s/%s' % (build.bucket, build.channel, build.board,
                                 build.version)

  @staticmethod
  def BuildPayloadsUri(build):
    """Creates the gspath for the payloads of a given build.

    Args:
      build: An instance of gspaths.Build that defines the build.

    Returns:
      The url for the specified build's payloads. Should be of the form:
        gs://chromeos-releases/blah-channel/board-name/1.2.3/payloads
    """
    return os.path.join(ChromeosReleases.BuildUri(build), 'payloads')

  @staticmethod
  def BuildPayloadsSigningUri(build):
    """Creates the base gspath for payload signing files.

    We create a number of files during signer interaction. This method creates
    the base path for all such files associated with a given build. There
    should still be subdirectories per-payload to avoid collisions, but by
    using this uniform base pass clean up can be more reliable.

    Args:
      build: An instance of gspaths.Build that defines the build.

    Returns:
      The url for the specified build's payloads. Should be of the form:
      gs://chromeos-releases/blah-channel/board-name/1.2.3/payloads/signing
    """
    return os.path.join(ChromeosReleases.BuildPayloadsUri(build), 'signing')

  @staticmethod
  def BuildPayloadsFlagUri(build, flag):
    """Creates the gspath for a given build flag.

    LOCK - means that payload processing is in progress on the host which
           owns the locks. Locks have a timeout associated with them in
           case of error, but are not 100% atomic when a lock is timing out.

    Args:
      build: An instance of gspaths.Build that defines the build.
      flag: gs_paths.LOCK

    Returns:
      The url for the specified build's payloads. Should be of the form:
      gs://chromeos-releases/blah-channel/board-name/1.2.3/payloads/LOCK_FLAG
    """
    assert flag in ChromeosReleases.FLAGS
    return os.path.join(ChromeosReleases.BuildPayloadsUri(build),
                        '%s_flag' % flag)

  @staticmethod
  def ImageName(channel, board, version, key, image_type):
    """Creates the base file name for a given build image.

    Args:
      channel: What channel does the build belong too. Usually xxx-channel.
      board: What board is the build for? "x86-alex", "lumpy", etc.
      version: "What is the build version. "3015.0.0", "1945.76.3", etc
      key: "What is the signing key. "premp", "mp", "mp-v2", etc
      image_type: The type of image.  It can be either "recovery" or "base".

    Returns:
      The name of the specified image. Should be of the form:
        chromeos_1.2.3_board-name_recovery_blah-channel_key.bin
    """

    template = ('chromeos_%(version)s_%(board)s_%(image_type)s'
                + '_%(channel)s_%(key)s.bin')

    return template % {
        'channel': channel,
        'board': board,
        'version': version,
        'key': key,
        'image_type': image_type,
    }

  @staticmethod
  def DLCImageName():
    """Creates file name for a DLC image.

    Returns:
      The name of the DLC image.
    """
    return 'dlc.img'

  @staticmethod
  def UnsignedImageArchiveName(board, version, milestone, image_type):
    """The base name for the tarball containing an unsigned build image.

    Args:
      board: What board is the build for? "x86-alex", "lumpy", etc.
      version: What is the build version? "3015.0.0", "1945.76.3", etc
      milestone: the most recent branch corresponding to the version; "R19" etc
      image_type: either "recovery" or "test", currently

    Returns:
      The name of the specified image archive. Should be of the form:
        ChromeOS-type-R19-1.2.3-board-name.tar.xz
    """

    template = (
        'ChromeOS-%(image_type)s-%(milestone)s-%(version)s-%(board)s.tar.xz')

    return template % {
        'board': board,
        'version': version,
        'milestone': milestone,
        'image_type': image_type,
    }

  @staticmethod
  def ImageUri(build, key, image_type, image_channel=None, image_version=None):
    """Creates the gspath for a given build image.

    Args:
      build: An instance of gspaths.Build that defines the build.
      key: What is the signing key? "premp", "mp", "mp-v2", etc
      image_type: The type of image.  It can be either "recovery" or "base".
      image_channel: Sometimes an image has a different channel than the build
                     directory it's in. (ie: nplusone).
      image_version: Sometimes an image has a different version than the build
                     directory it's in. (ie: nplusone).

    Returns:
      The url for the specified build's image. Should be of the form:
        gs://chromeos-releases/blah-channel/board-name/1.2.3/
          chromeos_1.2.3_board-name_recovery_blah-channel_key.bin
    """
    if not image_channel:
      image_channel = build.channel

    if not image_version:
      image_version = build.version

    return os.path.join(ChromeosReleases.BuildUri(build),
                        ChromeosReleases.ImageName(image_channel, build.board,
                                                   image_version, key,
                                                   image_type))

  @staticmethod
  def UnsignedImageUri(build, milestone, image_type):
    """Creates the gspath for a given unsigned build image archive.

    Args:
      build: An instance of gspaths.Build that defines the build.
      milestone: the most recent branch corresponding to the version; "R19" etc
      image_type: either "recovery" or "test", currently

    Returns:
      The url for the specified build's image. Should be of the form:
        gs://chromeos-releases/blah-channel/board-name/1.2.3/
          ChromeOS-type-R19-1.2.3-board-name.tar.xz
    """
    return os.path.join(
        ChromeosReleases.BuildUri(build),
        ChromeosReleases.UnsignedImageArchiveName(build.board, build.version,
                                                  milestone, image_type))

  @staticmethod
  def DLCImagesUri(build):
    """Creates the gspath for DLC images for a given build image archive."""

    # DLC images are located at gs://{path_to_build}/dlc/{DLC_ID}/{DLC_PACKAGE}
    return os.path.join(ChromeosReleases.BuildUri(build), 'dlc', '*', '*',
                        ChromeosReleases.DLCImageName())

  @classmethod
  def ParseImageUri(cls, image_uri):
    """Parse the URI of an image into an Image object."""

    # The named values in this regex must match the arguments to gspaths.Image.
    exp = (r'^gs://(?P<bucket>.*)/(?P<channel>.*)/(?P<board>.*)/'
           r'(?P<version>.*)/chromeos_(?P<image_version>[^_]+)_'
           r'(?P=board)_(?P<image_type>[^_]+)_(?P<image_channel>[^_]+)_'
           '(?P<key>[^_]+).bin$')

    values = Build.BuildValuesFromUri(exp, image_uri)
    if not values:
      return None

    # Insert the URI.
    values['uri'] = image_uri

    # Create an Image object using the values we parsed out.
    return Image(values)

  @classmethod
  def ParseUnsignedImageUri(cls, image_uri):
    """Parse the URI of an image into an UnsignedImageArchive object."""

    # The named values in this regex must match the arguments to gspaths.Image.
    exp = (r'gs://(?P<bucket>[^/]+)/(?P<channel>[^/]+)/'
           r'(?P<board>[^/]+)/(?P<version>[^/]+)/'
           r'ChromeOS-(?P<image_type>%s)-(?P<milestone>R[0-9]+)-'
           r'(?P=version)-(?P=board).tar.xz' %
           '|'.join(cls.UNSIGNED_IMAGE_TYPES))

    values = Build.BuildValuesFromUri(exp, image_uri)
    if not values:
      return None

    # Insert the URI.
    values['uri'] = image_uri

    # Create an Image object using the values we parsed out.
    return UnsignedImageArchive(values)

  @classmethod
  def ParseDLCImageUri(cls, image_uri):
    """Parse the URI of a DLC image into an Image object."""

    # The named values in this regex must match the arguments to
    # gspaths.DLCImage.
    exp = (r'^gs://(?P<bucket>.*)/(?P<channel>.*)/(?P<board>.*)/'
           r'(?P<version>.*)/dlc/(?P<dlc_id>.*)/(?P<dlc_package>.*)/'
           r'(?P<dlc_image>.*)$')

    values = Build.BuildValuesFromUri(exp, image_uri)
    if not values:
      logging.warning('Unparsable DLC URI: %s', image_uri)
      return None

    # Insert the URI
    values['uri'] = image_uri

    # Create an Image object using the values we parsed out.
    return DLCImage(values)

  @staticmethod
  def DLCPayloadName(channel, board, version, dlc_id, dlc_package,
                     random_str=None, src_version=None, sign=True):
    """Creates the payload file name of a DLC image.

    Args:
      channel: What channel does the build belong to? Usually "xxx-channel".
      board: What board is the build for? "x86-alex", "lumpy", etc.
      version: What is the build version? "3015.0.0", "1945.76.3", etc
      dlc_id: This is the ID of the DLC module.
      dlc_package: Package name of the DLC module.
      random_str: Force a given random string. None means generate one.
      src_version: If this payload is a delta, this is the version of the image
                   it updates from.
      sign: Whether to sign the payload.

    Returns:
      The name for the specified build's payloads. Should be in the form of:
      dlc_dummy-dlc_dummy-package_11869.0.0_kevin-arcnext_canary-channel_full
      .bin-250bc111ea4955aebc2af08db1f1773c.signed
    """
    if random_str is None:
      random_str = _RandomString()

    if sign is True:
      signed_ext = '.signed'
    else:
      signed_ext = ''

    if src_version:
      template = ('dlc_%(dlc_id)s_%(dlc_package)s_%(src_version)s-%(version)s_'
                  '%(board)s_%(channel)s_delta.bin-%(random_str)s'
                  '%(signed_ext)s')

      return template % {
          'dlc_id' : dlc_id,
          'dlc_package': dlc_package,
          'channel': channel,
          'board': board,
          'version': version,
          'random_str': random_str,
          'src_version': src_version,
          'signed_ext': signed_ext,
      }
    else:
      template = ('dlc_%(dlc_id)s_%(dlc_package)s_%(version)s_%(board)s_'
                  '%(channel)s_full.bin-%(random_str)s%(signed_ext)s')

      return template % {
          'dlc_id' : dlc_id,
          'dlc_package': dlc_package,
          'channel': channel,
          'board': board,
          'version': version,
          'random_str': random_str,
          'signed_ext': signed_ext,
      }

  @staticmethod
  def PayloadName(channel, board, version, key=None, random_str=None,
                  src_version=None, unsigned_image_type='test'):
    """Creates the gspath for a payload associated with a given build.

    Args:
      channel: What channel does the build belong to? Usually "xxx-channel".
      board: What board is the build for? "x86-alex", "lumpy", etc.
      version: What is the build version? "3015.0.0", "1945.76.3", etc
      key: What is the signing key? "premp", "mp", "mp-v2", etc; None (default)
           indicates that the image is not signed, e.g. a test image
      image_channel: Sometimes an image has a different channel than the build
                     directory it's in. (ie: nplusone).
      image_version: Sometimes an image has a different version than the build
                     directory it's in. (ie: nplusone).
      random_str: Force a given random string. None means generate one.
      src_version: If this payload is a delta, this is the version of the image
                   it updates from.
      unsigned_image_type: the type descriptor (string) of an unsigned image;
                           significant iff key is None (default: "test")

    Returns:
      The name for the specified build's payloads. Should be of the form:

        chromeos_0.12.433.257-2913.377.0_x86-alex_stable-channel_
        delta_mp-v3.bin-b334762d0f6b80f471069153bbe8b97a.signed

        chromeos_2913.377.0_x86-alex_stable-channel_full_mp-v3.
        bin-610c97c30fae8561bde01a6116d65cb9.signed
    """
    if random_str is None:
      random_str = _RandomString()

    if key is None:
      signed_ext = ''
      key = unsigned_image_type
    else:
      signed_ext = '.signed'

    if src_version:
      template = ('chromeos_%(src_version)s-%(version)s_%(board)s_%(channel)s_'
                  'delta_%(key)s.bin-%(random_str)s%(signed_ext)s')

      return template % {
          'channel': channel,
          'board': board,
          'version': version,
          'key': key,
          'random_str': random_str,
          'src_version': src_version,
          'signed_ext': signed_ext,
      }
    else:
      template = ('chromeos_%(version)s_%(board)s_%(channel)s_'
                  'full_%(key)s.bin-%(random_str)s%(signed_ext)s')

      return template % {
          'channel': channel,
          'board': board,
          'version': version,
          'key': key,
          'random_str': random_str,
          'signed_ext': signed_ext,
      }

  @staticmethod
  def DLCPayloadUri(build, random_str, dlc_id, dlc_package, image_channel=None,
                    image_version=None, src_version=None):
    """Creates the gspath for a payload associated with a given build.

    Args:
      build: An instance of gspaths.Build that defines the build.
      random_str: Force a given random string. None means generate one.
      dlc_id: This is the ID of the DLC module.
      dlc_package: This is the package name of the DLC module.
      image_channel: Sometimes an image has a different channel than the build
                     directory it's in. (ie: nplusone).
      image_version: Sometimes an image has a different version than the build
                     directory it's in. (ie: nplusone).
      src_version: If this payload is a delta, this is the version of the image
                   it updates from.
    """
    if image_channel is None:
      image_channel = build.channel

    if image_version is None:
      image_version = build.version

    # DLC payloads are pushed to dlc/|dlc_id|/|dlc_package| subfolder.
    return os.path.join(ChromeosReleases.BuildPayloadsUri(build),
                        'dlc', dlc_id, dlc_package,
                        ChromeosReleases.DLCPayloadName(image_channel,
                                                        build.board,
                                                        image_version,
                                                        dlc_id,
                                                        dlc_package,
                                                        random_str,
                                                        src_version))

  @staticmethod
  def PayloadUri(build, random_str, key=None, image_channel=None,
                 image_version=None, src_version=None):
    """Creates the gspath for a payload associated with a given build.

    Args:
      build: An instance of gspaths.Build that defines the build.
      key: What is the signing key? "premp", "mp", "mp-v2", etc; None means
           that the image is unsigned (e.g. a test image)
      image_channel: Sometimes an image has a different channel than the build
                     directory it's in. (ie: nplusone).
      image_version: Sometimes an image has a different version than the build
                     directory it's in. (ie: nplusone).
      random_str: Force a given random string. None means generate one.
      src_version: If this payload is a delta, this is the version of the image
                   it updates from.

    Returns:
      The url for the specified build's payloads. Should be of the form:

        gs://chromeos-releases/stable-channel/x86-alex/2913.377.0/payloads/
          chromeos_0.12.433.257-2913.377.0_x86-alex_stable-channel_
          delta_mp-v3.bin-b334762d0f6b80f471069153bbe8b97a.signed

        gs://chromeos-releases/stable-channel/x86-alex/2913.377.0/payloads/
          chromeos_2913.377.0_x86-alex_stable-channel_full_mp-v3.
          bin-610c97c30fae8561bde01a6116d65cb9.signed
    """
    if image_channel is None:
      image_channel = build.channel

    if image_version is None:
      image_version = build.version

    return os.path.join(ChromeosReleases.BuildPayloadsUri(build),

                        ChromeosReleases.PayloadName(image_channel,
                                                     build.board,
                                                     image_version,
                                                     key,
                                                     random_str,
                                                     src_version))

  @classmethod
  def ParsePayloadUri(cls, payload_uri):
    """Parse the URI of an image into an Image object."""

    # Sample Delta URI:
    #   gs://chromeos-releases/stable-channel/x86-mario/4731.72.0/payloads/
    #   chromeos_4537.147.0-4731.72.0_x86-mario_stable-channel_delta_mp-v3.bin-
    #   3a90d8666d1d42b7a7367660b897e8c9.signed

    # Sample Full URI:
    # gs://chromeos-releases/stable-channel/x86-mario/4731.72.0/payloads/
    #   chromeos_4731.72.0_x86-mario_stable-channel_full_mp-v3.bin-
    #   969f24ba8cbf2096ebe3c57d5f0253b7.signed

    # Handle FULL payload URIs.
    full_exp = (r'^gs://(?P<bucket>.*)/(?P<channel>.*)/(?P<board>.*)/'
                r'(?P<version>.*)/payloads/chromeos_(?P<image_version>[^_]+)_'
                r'(?P=board)_(?P<image_channel>[^_]+)_full_(?P<key>[^_]+)\.bin'
                r'-[0-9A-Fa-f]+\.signed$')

    image_values = Build.BuildValuesFromUri(full_exp, payload_uri)
    if image_values:
      # The image URIs can't be discovered from the payload URI.
      image_values['uri'] = None

      # Create the Payload.
      tgt_image = Image(image_values)

      return Payload(tgt_image=tgt_image, uri=payload_uri)

    # Handle DELTA payload URIs.
    delta_exp = (r'^gs://(?P<bucket>.*)/(?P<channel>.*)/(?P<board>.*)/'
                 r'(?P<version>.*)/payloads/chromeos_(?P<src_version>[^_]+)-'
                 r'(?P<image_version>[^_]+)_(?P=board)_'
                 r'(?P<image_channel>[^_]+)_delta_(?P<key>[^_]+)\.bin'
                 r'-[0-9A-Fa-f]+\.signed$')

    image_values = Build.BuildValuesFromUri(delta_exp, payload_uri)
    if image_values:
      # The image URIs can't be discovered from the payload URI.
      image_values['uri'] = None

      # Remember the src_version for the src_image.
      src_version = image_values['src_version']
      del image_values['src_version']

      # Create the payload.
      tgt_image = Image(image_values)

      # Create a new Build so we don't override the one in the target image.
      image_values['build'] = Build(image_values['build'])
      # Set the values which are different for src versions.
      image_values['build']['version'] = src_version

      # The payload URI doesn't tell us any of these values. However, it's
      # a mostly safe bet that the src version has no
      # image_version/image_channel.
      # Not knowing the source key is problematic.
      image_values['image_version'] = None
      image_values['image_channel'] = None
      image_values['key'] = None

      src_image = Image(image_values)

      return Payload(src_image=src_image, tgt_image=tgt_image, uri=payload_uri)

    # The URI didn't match.
    return None


class ChromeosImageArchive(object):
  """Name space class for static methods for URIs in chromeos-image-archive."""

  BUCKET = 'chromeos-image-archive'

  @classmethod
  def BuildUri(cls, board, milestone, version, bucket=None):
    """Creates the gspath for a given build.

    Args:
      board: What board is the build for? "x86-alex", "lumpy", etc.
      milestone: a number that defines the milestone mark, e.g. 19 for R19
      version: "What is the build version. "3015.0.0", "1945.76.3", etc
      bucket: the bucket the build in (None means cls.BUCKET)

    Returns:
      The url for the specified build artifacts. Should be of the form:
      gs://chromeos-image-archive/board-release/R23-4.5.6
    """

    bucket = bucket or cls.BUCKET

    return 'gs://%s/%s-release/R%s-%s' % (bucket, board, milestone, version)


def VersionKey(version):
  """Convert a version string to a comparable value.

  All old style values are considered older than all new style values.
  The actual values returned should only be used for comparison against
  other VersionKey results.

  Args:
    version: String with a build version "1.2.3" or "0.12.3.4"

  Returns:
    A value comparable against other version strings.
  """

  key = [int(n) for n in version.split('.')]

  # 3 number versions are new style.
  # 4 number versions are old style.
  assert len(key) in (3, 4)

  if len(key) == 3:
    # 1.2.3 -> (1, 0, 1, 2, 3)
    return [1, 0] + key
  else:
    # 0.12.3.4 -> (0, 0, 12, 3, 4)
    return [0] + key


def VersionGreater(left, right):
  """Compare two version strings. left > right

  Args:
    left: String with lefthand version string "1.2.3" or "0.12.3.4"
    right: String with righthand version string "1.2.3" or "0.12.3.4"

  Returns:
    left > right taking into account new style versions versus old style.
  """
  return VersionKey(left) > VersionKey(right)


def IsImage(a):
  """Return if the object is of Image type.

  Args:
    a: object whose type needs to be checked

  Returns:
    True if |a| is of Image type, False otherwise
  """
  return isinstance(a, Image)


def IsUnsignedImageArchive(a):
  """Return if the object is of UnsignedImageArchive type.

  Args:
    a: object whose type needs to be checked

  Returns:
    True if |a| is of UnsignedImageArchive type, False otherwise
  """
  return isinstance(a, UnsignedImageArchive)


def IsDLCImage(a):
  """Return if the object is of DLCImage type.

  Args:
    a: object whose type needs to be checked

  Returns:
    True if |a| is of DLCImage type, False otherwise
  """
  return isinstance(a, DLCImage)


def _RandomString():
  """Helper function for generating a random string for a payload name.

  This is an external helper function so that it can be trivially mocked
  to make test results deterministic.
  """
  random.seed()
  return hashlib.md5(str(random.getrandbits(128))).hexdigest()
