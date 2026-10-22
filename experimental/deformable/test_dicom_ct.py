from pathlib import Path
import tempfile
import unittest

import numpy as np
from pydicom.dataset import FileDataset, FileMetaDataset
from pydicom.uid import ExplicitVRLittleEndian, CTImageStorage, generate_uid

from dicom_ct import read_ct, normalize_mask, window_hu


class DicomCtTests(unittest.TestCase):
    def test_signed_pixels_rescale_padding_and_physical_coordinates(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original.dcm"
            meta = FileMetaDataset()
            meta.TransferSyntaxUID = ExplicitVRLittleEndian
            meta.MediaStorageSOPClassUID = CTImageStorage
            meta.MediaStorageSOPInstanceUID = generate_uid()
            ds = FileDataset(str(path), {}, file_meta=meta, preamble=b"\0" * 128)
            ds.Modality = "CT"
            ds.Rows = ds.Columns = 8
            ds.SamplesPerPixel = 1
            ds.PhotometricInterpretation = "MONOCHROME2"
            ds.BitsAllocated = ds.BitsStored = 16
            ds.HighBit = 15
            ds.PixelRepresentation = 1
            ds.PixelSpacing = [2., 1.]
            ds.ImagePositionPatient = [10., 20., 30.]
            ds.ImageOrientationPatient = [1., 0., 0., 0., 1., 0.]
            ds.RescaleSlope, ds.RescaleIntercept = 2., -1024.
            ds.PixelPaddingValue = -2000
            pixels = np.full((8, 8), 512, np.int16)
            pixels[0, 0] = -2000
            ds.PixelData = pixels.tobytes()
            ds.save_as(path, enforce_file_format=True)
            ct = read_ct(path)
            self.assertEqual(float(ct.hu[1, 1]), 0.)
            self.assertFalse(ct.valid[0, 0])
            patient = np.asarray(ct.metadata["native_pixel_xy_to_patient_mm_4x4"])
            np.testing.assert_allclose(patient @ [3, 4, 0, 1], [13, 28, 30, 1])
            self.assertEqual(window_hu(ct.hu).shape, (8, 8, 3))

    def test_anisotropic_mask_resampling_has_explicit_transform(self):
        mask = np.ones((20, 30), np.uint8)
        normalized, transform = normalize_mask(mask, [2., 1.], .5)
        self.assertEqual(normalized.shape, (77, 59))
        np.testing.assert_allclose(transform @ [3, 4, 1], [6, 16, 1])
        with self.assertRaises(ValueError):
            normalize_mask(mask, [2., 1.], 0)


if __name__ == "__main__":
    unittest.main()
