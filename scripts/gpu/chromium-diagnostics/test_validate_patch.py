#!/usr/bin/env python3
"""Exercise application/refusal against real pinned sources, never C++ stubs."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import validate_patch


SOURCE_TREE = None
MANIFEST = json.loads((Path(__file__).parent / "upstream-sources.json").read_text())


class PatchApplicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="chromium-bt-apply-")
        self.addCleanup(self.temp.cleanup)
        self.checkout = Path(self.temp.name)
        for row in MANIFEST["sources"]:
            source = validate_patch.regular_file(SOURCE_TREE, row["path"])
            if validate_patch.digest(source) != row["sha256"]:
                self.fail(f"test requires actual pinned source: {row['path']}")
            target = self.checkout / row["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        subprocess.run(["git", "init", "-q", str(self.checkout)], check=True)

    def test_real_source_check_apply_and_output_hashes(self):
        result = validate_patch.validate(self.checkout)
        self.assertFalse(result["applied"])
        result = validate_patch.validate(self.checkout, apply=True)
        self.assertTrue(result["applied"])
        self.assertEqual(result["compilation"], "not performed")
        self.assertEqual(len(result["verified_outputs"]), 15)
        submitter = self.checkout / "third_party/blink/renderer/platform/graphics/video_frame_submitter.cc"
        text = submitter.read_text()
        conditions = [
            "if (!compositor_frame_sink_ || !ShouldSubmit())",
            "if (last_frame_id_ == video_frame->unique_id())",
            "if (frame_size.IsEmpty())",
            "if (frame_size_ != frame_size)",
            "if (waiting_for_compositor_ack_ > 0 && !frame_size_changed)",
        ]
        positions = [text.index(condition) for condition in conditions]
        self.assertEqual(positions, sorted(positions))
        begin = text.index("bool VideoFrameSubmitter::SubmitFrame(")
        end = text.index("void VideoFrameSubmitter::SubmitEmptyFrame()", begin)
        body = text[begin:end]
        self.assertLess(body.index("resource_provider_->ReleaseFrameResources();"),
                        body.index("NotifyOpacityIfNeeded(new_opacity);"))
        self.assertLess(body.index("NotifyOpacityIfNeeded(new_opacity);"),
                        body.index("++waiting_for_compositor_ack_;"))
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            validate_patch.validate(self.checkout, apply=True)

    def test_modified_upstream_is_refused_before_application(self):
        path = self.checkout / MANIFEST["sources"][0]["path"]
        path.write_bytes(path.read_bytes() + b"\n// unrelated change\n")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            validate_patch.validate(self.checkout, apply=True)
        self.assertFalse((self.checkout / MANIFEST["new_files"][0]).exists())

    def test_prior_instrumentation_delta_reproduces_full_patch(self):
        validate_patch.validate(self.checkout, apply=True)
        raw_ref_delta = Path(__file__).resolve().parent / "raw-ref-recorder.delta.patch"
        validate_patch.git(self.checkout, "apply", "--reverse", str(raw_ref_delta))
        delta = Path(__file__).resolve().parent / "surface-ack-completion.delta.patch"
        validate_patch.git(self.checkout, "apply", "--reverse", "--check", str(delta))
        validate_patch.git(self.checkout, "apply", "--reverse", str(delta))
        result = validate_patch.validate(self.checkout, apply=True, delta=True)
        self.assertTrue(result["applied"])
        raw_ref_manifest = json.loads(raw_ref_delta.with_suffix('.json').read_text())
        self.assertEqual(result["to_full_patch_sha256"], raw_ref_manifest["from_full_patch_sha256"])
        result = validate_patch.validate(self.checkout, apply=True, raw_ref_delta=True)
        self.assertEqual(result["verified_outputs"], MANIFEST["patched_sha256"])
        surface = (self.checkout / "components/viz/service/surfaces/surface.cc").read_text()
        call = surface.index("client->SendCompositorFrameAck();")
        done = surface.index('bt_record("surface.ack.done");', call)
        self.assertNotIn("frame.", surface[call:done])
        self.assertNotIn("client->", surface[call + len("client->SendCompositorFrameAck();"):done])

    def test_raw_ref_delta_preserves_output_and_borrow_lifetime(self):
        validate_patch.validate(self.checkout, apply=True)
        delta = Path(__file__).resolve().parent / "raw-ref-recorder.delta.patch"
        validate_patch.git(self.checkout, "apply", "--reverse", str(delta))
        result = validate_patch.validate(self.checkout, apply=True, raw_ref_delta=True)
        self.assertEqual(result["to_full_patch_sha256"], MANIFEST["patch_sha256"])
        self.assertEqual(result["verified_outputs"], MANIFEST["patched_sha256"])
        header = (self.checkout / 'base/trace_event/video_bottleneck_diagnostics.h').read_text()
        self.assertIn('raw_ref<TracedValue> value_;', header)
        self.assertNotIn('TracedValue& value_;', header)
        self.assertIn('std::forward<Fill>(fill)(fields);\n    }\n    TRACE_EVENT_INSTANT', header)
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            validate_patch.validate(self.checkout, apply=True, raw_ref_delta=True)

    def test_existing_new_file_is_preserved(self):
        path = self.checkout / MANIFEST["new_files"][0]
        path.write_text("preserve this file\n")
        with self.assertRaisesRegex(ValueError, "already exists"):
            validate_patch.validate(self.checkout, apply=True)
        self.assertEqual(path.read_text(), "preserve this file\n")

    def test_symlink_source_is_refused(self):
        relative = MANIFEST["sources"][0]["path"]
        path = self.checkout / relative
        path.unlink()
        path.symlink_to(SOURCE_TREE / relative)
        with self.assertRaisesRegex(ValueError, "symlink"):
            validate_patch.validate(self.checkout, apply=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-tree", type=Path, required=True,
                        help="Unmodified pinned checkout or retained source tree")
    args, remaining = parser.parse_known_args()
    SOURCE_TREE = args.source_tree.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
