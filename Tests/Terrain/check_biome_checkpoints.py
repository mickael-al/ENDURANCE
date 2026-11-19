#!/usr/bin/env python3
"""Exercise interrupted biome diagnostics and resume without opening OpenGL."""
import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

import check_biome_landforms as landforms


class CheckpointTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='biome-checkpoints-')
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        self.checkpoint = self.output / 'biome_landform_metrics.json'
        self.sample_path = self.output / 'biome_00_samples.npz'
        self.record = {'id': 0, 'seed': 1337, 'source_sha256': 'current',
                       'scene_uniforms': dict(landforms.SCENE_OPTIONS)}

    def save_sample(self, **changes):
        density = np.zeros((2, 2, 2), dtype=np.float32)
        sample = dict(density=density, columns=np.zeros((2, 2, 4)),
                      origin=(0., 0.), spacing=1., min_y=0., step_y=1.,
                      close_density=density, close_columns=np.zeros((2, 2, 4)),
                      close_origin=(0., 0.), close_spacing=1., close_min_y=0., close_step_y=1.,
                      landscape_height=np.zeros((2, 2)), landscape_origin=(0., 0.),
                      seed=1337, biome=0)
        sample.update(changes)
        np.savez(self.sample_path, **sample)

    def save_checkpoint(self, record=None):
        landforms.write_checkpoint(self.output, [record or self.record], 'test renderer')

    def run_resume(self, *extra):
        argv = ['check_biome_landforms', '--output', str(self.output),
                '--resume', '--no-render', '--biomes', '0', *extra]
        with patch('sys.argv', argv), patch.object(landforms, 'source_fingerprint', return_value='current'), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return landforms.main()

    def test_failed_publication_preserves_previous_checkpoint(self):
        self.save_checkpoint()
        original = self.checkpoint.read_bytes()

        def interrupted_dump(payload, file, **kwargs):
            file.write('{incomplete')
            raise OSError('simulated interrupted write')

        for target, replacement in [('json.dump', interrupted_dump),
                                    ('pathlib.Path.replace', OSError('simulated failed rename'))]:
            with self.subTest(target=target), patch(target, side_effect=replacement):
                with self.assertRaises(OSError):
                    landforms.write_checkpoint(self.output, [], 'replacement')
            self.assertEqual(self.checkpoint.read_bytes(), original)
            self.assertFalse(list(self.output.glob('.biome_metrics_*.tmp')))
        landforms.write_checkpoint(self.output, [{'id': 3}, {'id': 1}], 'new')
        self.assertEqual([r['id'] for r in json.loads(self.checkpoint.read_text())['records']], [1, 3])

    def test_archives_must_be_complete_and_identify_the_record(self):
        self.assertFalse(landforms.valid_saved_sample(self.output, self.record))
        self.sample_path.write_bytes(b'')
        self.assertFalse(landforms.valid_saved_sample(self.output, self.record))
        self.save_sample()
        self.assertTrue(landforms.valid_saved_sample(self.output, self.record))
        archive = bytearray(self.sample_path.read_bytes())
        archive[archive.index(b'\x93NUMPY')] ^= 1  # Valid ZIP directory, damaged member CRC.
        self.sample_path.write_bytes(archive)
        self.assertFalse(landforms.valid_saved_sample(self.output, self.record))
        self.save_sample()
        self.sample_path.write_bytes(self.sample_path.read_bytes()[:-17])
        self.assertFalse(landforms.valid_saved_sample(self.output, self.record))
        for changes in ({'seed': 99}, {'biome': 1}):
            self.save_sample(**changes)
            self.assertFalse(landforms.valid_saved_sample(self.output, self.record))
        np.savez(self.sample_path, seed=1337, biome=0)
        self.assertFalse(landforms.valid_saved_sample(self.output, self.record))

    def test_completed_resume_never_opens_gl(self):
        self.save_checkpoint()
        self.save_sample()
        with patch.object(landforms, 'GL') as gl:
            self.assertEqual(self.run_resume('0'), 0)
            gl.assert_not_called()

    def test_stale_records_are_rejected_before_gl(self):
        self.save_sample()
        changes = [{'seed': 99}, {'source_sha256': 'stale'},
                   {'scene_uniforms': dict(landforms.SCENE_OPTIONS, uDetailScale=.7)}]
        for change in changes:
            record = copy.deepcopy(self.record)
            record.update(change)
            self.save_checkpoint(record)
            with self.subTest(change=change), patch.object(landforms, 'GL') as gl:
                with self.assertRaises(SystemExit) as error:
                    self.run_resume()
                self.assertEqual(error.exception.code, 2)
                gl.assert_not_called()

    def test_invalid_samples_are_recomputed_not_reused(self):
        self.save_checkpoint()
        self.sample_path.write_bytes(b'not a zip file')
        with patch.object(landforms, 'GL', side_effect=RuntimeError('work requested')) as gl:
            with self.assertRaisesRegex(RuntimeError, 'work requested'):
                self.run_resume()
            gl.assert_called_once()

    def test_renderer_change_is_rejected_before_new_measurements(self):
        self.save_checkpoint()
        self.save_sample()
        with patch.object(landforms, 'GL') as gl, patch.object(landforms, 'BiomeProbe') as probe:
            gl.return_value.__enter__.return_value.renderer = 'different renderer'
            with self.assertRaises(SystemExit) as error:
                self.run_resume('1')
            self.assertEqual(error.exception.code, 2)
            probe.assert_not_called()


if __name__ == '__main__':
    unittest.main()
