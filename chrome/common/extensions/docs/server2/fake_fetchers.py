# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# These are fake fetchers that are used for testing and the preview server.
# They return canned responses for URLs. appengine_wrappers.py uses the fake
# fetchers if the App Engine imports fail.

import base64
import json
import os
import re

import appengine_wrappers
from extensions_paths import SERVER2
from path_util import IsDirectory
from test_util import ReadFile, ChromiumPath
import url_constants


# TODO(kalman): Investigate why logging in this class implies that the server
# isn't properly caching some fetched files; often it fetches the same file
# 10+ times. This may be a test anomaly.


def _ReadTestData(*path, **read_args):
  return ReadFile(SERVER2, 'test_data', *path, **read_args)


class _FakeFetcher(object):
  def _ListDir(self, path):
    return os.listdir(path)

  def _IsDir(self, path):
    return os.path.isdir(path)

  def _Stat(self, path):
    return int(os.stat(path).st_mtime)


class _FakeOmahaProxy(_FakeFetcher):
  def fetch(self, url):
    return _ReadTestData('branch_utility', 'first.json')


class _FakeOmahaHistory(_FakeFetcher):
  def fetch(self, url):
    return _ReadTestData('branch_utility', 'second.json')


_SVN_URL_TO_PATH_PATTERN = re.compile(
    r'^.*chrome/.*(trunk|branches/.*)/src/?([^?]*).*?')
def _ExtractPathFromSvnUrl(url):
  return _SVN_URL_TO_PATH_PATTERN.match(url).group(2)


class _FakeSubversionServer(_FakeFetcher):
  def fetch(self, url):
    path = _ExtractPathFromSvnUrl(url)
    if IsDirectory(path):
      html = ['<html>Revision 000000']
      try:
        for f in self._ListDir(ChromiumPath(path)):
          if f.startswith('.'):
            continue
          if self._IsDir(ChromiumPath(path, f)):
            html.append('<a>' + f + '/</a>')
          else:
            html.append('<a>' + f + '</a>')
        html.append('</html>')
        return '\n'.join(html)
      except OSError as e:
        return None
    try:
      return ReadFile(path)
    except IOError:
      return None


_GITILES_BASE_RE = re.escape('%s/%s' %
    (url_constants.GITILES_BASE, url_constants.GITILES_SRC_ROOT))
_GITILES_BRANCH_BASE_RE = re.escape('%s/%s/%s' %
    (url_constants.GITILES_BASE,
     url_constants.GITILES_SRC_ROOT,
     url_constants.GITILES_BRANCHES_PATH))
# NOTE: _GITILES_BRANCH_BASE_RE must be first, because _GITILES_BASE_RE is
# a more general pattern.
_GITILES_URL_RE = r'(%s|%s)/' % (_GITILES_BRANCH_BASE_RE, _GITILES_BASE_RE)
_GITILES_URL_TO_COMMIT_PATTERN = re.compile(r'%s[^/]+$' % _GITILES_URL_RE)
_GITILES_URL_TO_PATH_PATTERN = re.compile(r'%s.+?/(.*)' % _GITILES_URL_RE)
def _ExtractPathFromGitilesUrl(url):
  return _GITILES_URL_TO_PATH_PATTERN.match(url).group(2)


class _FakeGitilesServer(_FakeFetcher):
  def fetch(self, url):
    if _GITILES_URL_TO_COMMIT_PATTERN.match(url) is not None:
      return json.dumps({'commit': '1' * 40})
    path = _ExtractPathFromGitilesUrl(url)
    chromium_path = ChromiumPath(path)
    if self._IsDir(chromium_path):
      jsn = {}
      dir_stat = self._Stat(chromium_path)
      jsn['id'] = dir_stat
      jsn['entries'] = []
      for f in self._ListDir(chromium_path):
        if f.startswith('.'):
          continue
        f_path = os.path.join(chromium_path, f)
        jsn['entries'].append({
          'id': self._Stat(f_path),
          'name': f,
          'type': 'tree' if self._IsDir(f_path) else 'blob'
        })
      return json.dumps(jsn)
    try:
      return base64.b64encode(ReadFile(path))
    except IOError:
      return None


class _FakeViewvcServer(_FakeFetcher):
  def fetch(self, url):
    path = ChromiumPath(_ExtractPathFromSvnUrl(url))
    if self._IsDir(path):
      html = ['<table><tbody><tr>...</tr>']
      # The version of the directory.
      dir_stat = self._Stat(path)
      html.append('<tr>')
      html.append('<td>Directory revision:</td>')
      html.append('<td><a>%s</a><a></a></td>' % dir_stat)
      html.append('</tr>')
      # The version of each file.
      for f in self._ListDir(path):
        if f.startswith('.'):
          continue
        html.append('<tr>')
        html.append('  <td><a>%s%s</a></td>' % (
            f, '/' if self._IsDir(os.path.join(path, f)) else ''))
        html.append('  <td><a><strong>%s</strong></a></td>' %
                    self._Stat(os.path.join(path, f)))
        html.append('<td></td><td></td><td></td>')
        html.append('</tr>')
      html.append('</tbody></table>')
      return '\n'.join(html)
    try:
      return ReadFile(path)
    except IOError:
      return None


class _FakeGithubStat(_FakeFetcher):
  def fetch(self, url):
    return '{ "sha": 0 }'


class _FakeGithubZip(_FakeFetcher):
  def fetch(self, url):
    return _ReadTestData('github_file_system', 'apps_samples.zip', mode='rb')


class _FakeRietveldAPI(_FakeFetcher):
  def __init__(self):
    self._base_pattern = re.compile(r'.*/(api/.*)')

  def fetch(self, url):
    return _ReadTestData(
        'rietveld_patcher', self._base_pattern.match(url).group(1), 'json')


class _FakeRietveldTarball(_FakeFetcher):
  def __init__(self):
    self._base_pattern = re.compile(r'.*/(tarball/\d+/\d+)')

  def fetch(self, url):
    return _ReadTestData(
        'rietveld_patcher', self._base_pattern.match(url).group(1) + '.tar.bz2',
        mode='rb')


def ConfigureFakeFetchers():
  '''Configure the fake fetcher paths relative to the docs directory.
  '''
  appengine_wrappers.ConfigureFakeUrlFetch({
    url_constants.OMAHA_HISTORY: _FakeOmahaHistory(),
    url_constants.OMAHA_PROXY_URL: _FakeOmahaProxy(),
    '%s/.*' % url_constants.SVN_URL: _FakeSubversionServer(),
    '%s/.*' % url_constants.VIEWVC_URL: _FakeViewvcServer(),
    '%s/.*/commits/.*' % url_constants.GITHUB_REPOS: _FakeGithubStat(),
    '%s/.*/zipball' % url_constants.GITHUB_REPOS: _FakeGithubZip(),
    '%s/api/.*' % url_constants.CODEREVIEW_SERVER: _FakeRietveldAPI(),
    '%s/tarball/.*' % url_constants.CODEREVIEW_SERVER: _FakeRietveldTarball(),
    '%s/.*' % _GITILES_BASE_RE: _FakeGitilesServer(),
    '%s/.*' % _GITILES_BRANCH_BASE_RE: _FakeGitilesServer()
  })
