"""Synthetic public-release gates; no proprietary game data or GitHub access."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_release
import release
from package_test import runtime_zip


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cmake = self.root / 'CMakeLists.txt'
        self.readme = self.root / 'README.md'
        self.cmake.write_bytes(b'project(fta2-ce VERSION 1.2.3 LANGUAGES CXX)\r\n')
        self.readme.write_bytes(('**🚧 Development alpha · version 1.2.3.**\r\n'
                                 'Historical version 1.2.3 remains recorded.\r\n').encode('utf-8'))
        release.git(self.root, 'init')
        for key, value in (('user.name', 'Release Test'), ('user.email', 'test@example.invalid'),
                           ('commit.gpgsign', 'false'), ('tag.gpgsign', 'false'),
                           ('core.autocrlf', 'false'), ('core.hooksPath', str(self.root / 'hooks'))):
            release.git(self.root, 'config', key, value)
        release.git(self.root, 'add', '.')
        release.git(self.root, 'commit', '-m', 'Initial fixture')
        self.head = release.git(self.root, 'rev-parse', 'HEAD')
        self.originals = {path: path.read_bytes() for path in (self.cmake, self.readme)}

    def assert_unchanged(self):
        for path, content in self.originals.items():
            self.assertEqual(path.read_bytes(), content)
        self.assertEqual(release.git(self.root, 'rev-parse', 'HEAD'), self.head)

    def test_patch_minor_major_reset_lower_components(self):
        for part, expected in (('patch', '1.2.4'), ('minor', '1.3.0'), ('major', '2.0.0')):
            with self.subTest(part=part):
                version, originals, changes = release.version_changes(self.root, part)
                self.assertEqual(version, expected)
                self.assertEqual(originals, self.originals)
                self.assertIn(f'VERSION {expected} '.encode(), changes[self.cmake])
                self.assertIn(f'version {expected}.**'.encode(), changes[self.readme])
                self.assertIn(b'Historical version 1.2.3 remains recorded.', changes[self.readme])
                self.assertTrue(all(content.endswith(b'\r\n') for content in changes.values()))
        self.assert_unchanged()

    def test_commit_and_annotated_tag_without_remote(self):
        for part, expected in (('patch', '1.2.4'), ('minor', '1.3.0'), ('major', '2.0.0')):
            with self.subTest(part=part):
                release.git(self.root, 'reset', '--hard', self.head)
                result = release.prepare_version(self.root, part)
                tag = 'v' + expected
                self.assertIn(f'git push origin HEAD {tag}', result)
                self.assertEqual(release.cmake_version(self.cmake.read_text()), expected)
                release.verify_tag(expected, tag)
                self.assertEqual(release.git(self.root, 'cat-file', '-t', tag), 'tag')
                self.assertEqual(release.git(self.root, 'rev-parse', tag + '^{commit}'),
                                 release.git(self.root, 'rev-parse', 'HEAD'))
                self.assertEqual(release.git(self.root, 'show', '--format=', '--name-only', 'HEAD').splitlines(),
                                 ['CMakeLists.txt', 'README.md'])
                self.assertEqual(release.git(self.root, 'status', '--porcelain'), '')

    def test_dirty_staged_and_untracked_files_rejected(self):
        for state in ('modified', 'staged', 'untracked'):
            with self.subTest(state=state):
                path = self.cmake if state != 'untracked' else self.root / 'notes.txt'
                before = path.read_bytes() if path.exists() else None
                path.write_bytes((before or b'') + b'work in progress\n')
                if state == 'staged':
                    release.git(self.root, 'add', '--', path.name)
                with self.assertRaisesRegex(ValueError, 'Commit or stash'):
                    release.prepare_version(self.root, 'patch')
                self.assertTrue(path.read_bytes().endswith(b'work in progress\n'))
                if before is None:
                    path.unlink()
                else:
                    path.write_bytes(before)
                release.git(self.root, 'reset', '--', path.name)
                self.assert_unchanged()

    def test_existing_tag_rejected_before_editing(self):
        release.git(self.root, 'tag', 'v1.2.4')
        with self.assertRaisesRegex(ValueError, 'Tag already exists'):
            release.prepare_version(self.root, 'patch')
        self.assert_unchanged()

    def test_mismatched_readme_rejected_before_editing(self):
        self.readme.write_text('Version missing', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'README'):
            release.prepare_version(self.root, 'patch')
        self.assertEqual(self.cmake.read_bytes(), self.originals[self.cmake])
        self.assertEqual(self.readme.read_text(), 'Version missing')

    def test_dry_run_does_not_change_files_or_git(self):
        self.assertIn('v1.3.0', release.prepare_version(self.root, 'minor', dry_run=True))
        self.assert_unchanged()
        self.assertEqual(release.git(self.root, 'tag', '--list'), '')

    def test_failed_tag_keeps_created_commit_and_reports_it(self):
        run_git = release.git
        def fail_tag(root, *args):
            if args[:2] == ('tag', '-a'):
                raise ValueError('Synthetic tag failure')
            return run_git(root, *args)
        with patch.object(release, 'git', side_effect=fail_tag):
            with self.assertRaisesRegex(ValueError, 'Version commit created, but v1.2.4 was not tagged'):
                release.prepare_version(self.root, 'patch')
        self.assertEqual(release.cmake_version(self.cmake.read_text()), '1.2.4')
        self.assertNotEqual(release.git(self.root, 'rev-parse', 'HEAD'), self.head)
        self.assertEqual(release.git(self.root, 'status', '--porcelain'), '')
        self.assertEqual(release.git(self.root, 'tag', '--list'), '')

    def test_failed_commit_restores_files_and_index(self):
        run_git = release.git
        def fail_commit(root, *args):
            if args[0] == 'commit':
                raise ValueError('Synthetic commit failure')
            return run_git(root, *args)
        with patch.object(release, 'git', side_effect=fail_commit):
            with self.assertRaisesRegex(ValueError, 'Synthetic commit failure'):
                release.prepare_version(self.root, 'patch')
        self.assert_unchanged()
        self.assertEqual(release.git(self.root, 'status', '--porcelain'), '')
        self.assertEqual(release.git(self.root, 'tag', '--list'), '')


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.packages = self.root / 'packages'
        self.assets = self.root / 'assets'
        self.packages.mkdir()
        self.assets.mkdir()
        self.version = '0.0.1'
        self.source = self.packages / 'fta2-ce-0.0.1-source.zip'
        with ZipFile(self.source, 'w') as archive:
            archive.writestr('fta2-ce-0.0.1-source/CMakeLists.txt',
                             'project(fta2-ce VERSION 0.0.1 LANGUAGES CXX)\n')
            archive.writestr('fta2-ce-0.0.1-source/LICENSE', 'GPL')
            archive.writestr('fta2-ce-0.0.1-source/assets/gameplay.png', b'authorized README preview')

    def engine(self, platform):
        path = self.packages / f'fta2-ce-{self.version}-{platform}.zip'
        with ZipFile(path, 'w') as archive:
            runtime_zip(archive, path.stem, platform=platform.split('-')[0])
        return path

    def player(self, platform='Linux-x86_64'):
        engine = self.engine(platform)
        destination = self.assets / engine.name
        package_release.package_release(engine, self.source, destination)
        return destination

    def rewrite(self, path, additions=None, remove=None):
        with ZipFile(path) as archive:
            contents = {info.filename: archive.read(info) for info in archive.infolist()
                        if info.filename != remove}
        contents.update(additions or {})
        with ZipFile(path, 'w') as archive:
            for name, content in contents.items():
                archive.writestr(name, content)

    def test_exact_version_tags(self):
        release.verify_tag(self.version, 'v0.0.1')
        for tag in ('0.0.1', 'v0.1.48', 'v0.0.01', 'v0.0.1-beta', 'v0.0.1\n', '$(echo v0.0.1)'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.verify_tag(self.version, tag)

    def test_cmake_version_is_authoritative(self):
        self.assertEqual(release.cmake_version('project(fta2-ce VERSION 0.0.1 LANGUAGES CXX)'), self.version)
        for content in ('project(other VERSION 0.0.1 LANGUAGES CXX)',
                        'project(fta2-ce VERSION 0.00.1 LANGUAGES CXX)', ''):
            with self.subTest(content=content), self.assertRaises(ValueError):
                release.cmake_version(content)

    def test_one_platform_build_embeds_audited_sources(self):
        engine = self.engine('Linux-x86_64')
        path = release.bundle(self.packages, self.assets, self.version)
        self.assertEqual(path.name, engine.name)
        release.audit_bundle(path, self.version)
        with self.assertRaises(ValueError):
            release.bundle(self.packages, self.assets, self.version)

    def test_ambiguous_build_output_rejected(self):
        self.engine('Linux-x86_64')
        self.engine('Windows-x86_64')
        with self.assertRaises(ValueError):
            release.bundle(self.packages, self.assets, self.version)
        self.assertFalse(list(self.assets.iterdir()))

    def test_all_platforms_and_checksums(self):
        for platform in release.PLATFORMS:
            self.player(platform)
        expected = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in self.assets.iterdir()}
        output = release.checksums(self.assets, self.version, all_platforms=True)
        actual = dict((name, digest) for digest, name in
                      (line.split('  ') for line in output.read_text().splitlines()))
        self.assertEqual(actual, expected)
        before = output.read_bytes()
        with self.assertRaises(ValueError):
            release.checksums(self.assets, self.version, all_platforms=True)
        self.assertEqual(output.read_bytes(), before)

    def test_missing_platform_blocks_publication(self):
        self.player()
        with self.assertRaisesRegex(ValueError, 'requires Linux'):
            release.checksums(self.assets, self.version, all_platforms=True)
        self.assertFalse((self.assets / 'SHA256SUMS').exists())

    def test_unexpected_asset_blocks_publication(self):
        self.player()
        (self.assets / 'private-complete.zip').write_bytes(b'must never publish')
        with self.assertRaises(ValueError):
            release.checksums(self.assets, self.version)
        self.assertFalse((self.assets / 'SHA256SUMS').exists())

    def test_runtime_game_data_or_missing_licence_rejected(self):
        for file in ('data/FTA.HRS', 'media/FTAVOICE.HRS', '999.SAV', 'screenshots/private.png'):
            with self.subTest(file=file):
                path = self.player()
                self.rewrite(path, {f'{path.stem}/{file}': b'private'})
                with self.assertRaises(ValueError):
                    release.audit_bundle(path, self.version)
                path.unlink()
        path = self.player()
        self.rewrite(path, remove=f'{path.stem}/licenses/LICENSE')
        with self.assertRaises(ValueError):
            release.audit_bundle(path, self.version)

    def test_source_manifest_tampering_rejected(self):
        path = self.player()
        self.rewrite(path, {f'{path.stem}/source/manifest.json': json.dumps({'sha256': 'invalid'})})
        with self.assertRaisesRegex(ValueError, 'checksum/size'):
            release.audit_bundle(path, self.version)

    def test_embedded_source_version_and_private_data_rejected(self):
        for filename, content in (('CMakeLists.txt', b'project(fta2-ce VERSION 0.1.48 LANGUAGES CXX)'),
                                  ('original-game/FTA.HRS', b'private'),
                                  ('.cache/private.png', b'private')):
            with self.subTest(filename=filename):
                original = self.source.read_bytes()
                self.rewrite(self.source, {f'fta2-ce-0.0.1-source/{filename}': content})
                try:
                    path = self.player()
                except ValueError:
                    pass  # Private source may already be rejected by the bundler.
                else:
                    with self.assertRaises(ValueError):
                        release.audit_bundle(path, self.version)
                    path.unlink()
                self.source.write_bytes(original)

    def test_archive_name_and_root_must_match(self):
        path = self.player()
        renamed = path.with_name('fta2-ce-0.0.1-Windows-x86_64.zip')
        path.rename(renamed)
        with self.assertRaisesRegex(ValueError, 'root'):
            release.audit_bundle(renamed, self.version)


if __name__ == '__main__':
    unittest.main()
