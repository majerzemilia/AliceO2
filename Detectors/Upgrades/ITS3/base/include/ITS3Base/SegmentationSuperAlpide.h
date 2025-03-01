// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file SegmentationSuperAlpide.h
/// \brief Definition of the SegmentationSuperAlpide class

#ifndef ALICEO2_ITS3_SEGMENTATIONSUPERALPIDE_H_
#define ALICEO2_ITS3_SEGMENTATIONSUPERALPIDE_H_

#include <Rtypes.h>
#include "MathUtils/Cartesian.h"
#include "CommonConstants/MathConstants.h"
#include <Framework/Logger.h>

namespace o2
{
namespace its3
{

/// Segmentation and response for pixels in ITS3 upgrade
class SegmentationSuperAlpide
{
 public:
  SegmentationSuperAlpide(int layer = 0) : mLayer{layer},
                                           mNPixels{mNRows * mNCols},
                                           mNRows{static_cast<int>(double(mRadii[layer] + mSensorLayerThickness / 2) * double(constants::math::PI) / double(mPitchRow) + 1)},
                                           mActiveMatrixSizeRows{mPitchRow * mNRows},
                                           mSensorSizeRows{mActiveMatrixSizeRows + mPassiveEdgeTop + mPassiveEdgeReadOut}
  {
    LOGP(info, "rows: {} cols: {} npixels: {}", mNRows, mNCols, mNPixels);
    LOGP(info, "SegmentationSuperAlpide: layer {} ActiveMatrixSizeRows: {} ActiveMatrixSizeCols: {}", mLayer, mActiveMatrixSizeCols, mActiveMatrixSizeRows);
  }
  static constexpr std::array<float, 10> mRadii = {1.8f, 2.4f, 3.0f, 7.0f, 10.f};
  static constexpr float mLength = 27.15f;
  static constexpr float mPitchCol = 20.e-4;
  static constexpr float mPitchRow = 20.e-4;
  static constexpr int mNCols = mLength / mPitchCol;
  int mNRows;
  int mNPixels;
  int mLayer;
  static constexpr float mPassiveEdgeReadOut = 0.;                   // width of the readout edge (Passive bottom)
  static constexpr float mPassiveEdgeTop = 0.;                       // Passive area on top
  static constexpr float mPassiveEdgeSide = 0.;                      // width of Passive area on left/right of the sensor
  static constexpr float mActiveMatrixSizeCols = mPitchCol * mNCols; // Active size along columns
  float mActiveMatrixSizeRows;                                       // Active size along rows

  // effective thickness of sensitive layer, accounting for charge collection non-unifoemity, https://alice.its.cern.ch/jira/browse/AOC-46
  static constexpr float mSensorLayerThicknessEff = 28.e-4;
  static constexpr float mSensorLayerThickness = 30.e-4;                                                // physical thickness of sensitive part
  static constexpr float mSensorSizeCols = mActiveMatrixSizeCols + mPassiveEdgeSide + mPassiveEdgeSide; // SensorSize along columns
  float mSensorSizeRows;                                                                                // SensorSize along rows

  ~SegmentationSuperAlpide() = default;

  /// Transformation from the curved surface to a flat surface
  /// It works only if the detector is not rototraslated
  /// \param xCurved Detector local curved coordinate x in cm with respect to
  /// the center of the sensitive volume.
  /// \param yCurved Detector local curved coordinate y in cm with respect to
  /// the center of the sensitive volulme.
  /// \param xFlat Detector local flat coordinate x in cm with respect to
  /// the center of the sensitive volume.
  /// \param yFlat Detector local flat coordinate y in cm with respect to
  /// the center of the sensitive volulme.
  void curvedToFlat(float xCurved, float yCurved, float& xFlat, float& yFlat);

  /// Transformation from the flat surface to a curved surface
  /// It works only if the detector is not rototraslated
  /// \param xFlat Detector local flat coordinate x in cm with respect to
  /// the center of the sensitive volume.
  /// \param yFlat Detector local flat coordinate y in cm with respect to
  /// the center of the sensitive volulme.
  /// \param xCurved Detector local curved coordinate x in cm with respect to
  /// the center of the sensitive volume.
  /// \param yCurved Detector local curved coordinate y in cm with respect to
  /// the center of the sensitive volulme.
  void flatToCurved(float xFlat, float yFlat, float& xCurved, float& yCurved);

  /// Transformation from Geant detector centered local coordinates (cm) to
  /// Pixel cell numbers iRow and iCol.
  /// Returns kTRUE if point x,z is inside sensitive volume, kFALSE otherwise.
  /// A value of -1 for iRow or iCol indicates that this point is outside of the
  /// detector segmentation as defined.
  /// \param float x Detector local coordinate x in cm with respect to
  /// the center of the sensitive volume.
  /// \param float z Detector local coordinate z in cm with respect to
  /// the center of the sensitive volulme.
  /// \param int iRow Detector x cell coordinate. Has the range 0 <= iRow < mNumberOfRows
  /// \param int iCol Detector z cell coordinate. Has the range 0 <= iCol < mNumberOfColumns
  bool localToDetector(float x, float z, int& iRow, int& iCol);
  /// same but w/o check for row/column range
  void localToDetectorUnchecked(float xRow, float zCol, int& iRow, int& iCol);

  /// Transformation from Detector cell coordiantes to Geant detector centered
  /// local coordinates (cm)
  /// \param int iRow Detector x cell coordinate. Has the range 0 <= iRow < mNumberOfRows
  /// \param int iCol Detector z cell coordinate. Has the range 0 <= iCol < mNumberOfColumns
  /// \param float x Detector local coordinate x in cm with respect to the
  /// center of the sensitive volume.
  /// \param float z Detector local coordinate z in cm with respect to the
  /// center of the sensitive volulme.
  /// If iRow and or iCol is outside of the segmentation range a value of -0.5*Dx()
  /// or -0.5*Dz() is returned.
  bool detectorToLocal(int iRow, int iCol, float& xRow, float& zCol);
  bool detectorToLocal(float row, float col, float& xRow, float& zCol);
  bool detectorToLocal(float row, float col, math_utils::Point3D<float>& loc);

  // same but w/o check for row/col range
  void detectorToLocalUnchecked(int iRow, int iCol, float& xRow, float& zCol);
  void detectorToLocalUnchecked(float row, float col, float& xRow, float& zCol);
  void detectorToLocalUnchecked(float row, float col, math_utils::Point3D<float>& loc);

  float getFirstRowCoordinate()
  {
    return 0.5 * ((mActiveMatrixSizeRows - mPassiveEdgeTop + mPassiveEdgeReadOut) - mPitchRow);
  }
  static constexpr float getFirstColCoordinate() { return 0.5 * (mPitchCol - mActiveMatrixSizeCols); }

  void print();

  ClassDefNV(SegmentationSuperAlpide, 1); // Segmentation class upgrade pixels
};

inline void SegmentationSuperAlpide::curvedToFlat(float xCurved, float yCurved, float& xFlat, float& yFlat)
{
  float dist = std::sqrt(xCurved * xCurved + yCurved * yCurved);
  float phi = (double)constants::math::PI / 2 - std::atan2((double)yCurved, (double)xCurved);
  xFlat = (mRadii[mLayer] + SegmentationSuperAlpide::mSensorLayerThickness / 2) * phi;
  yFlat = dist - (mRadii[mLayer] + SegmentationSuperAlpide::mSensorLayerThickness / 2);
}

inline void SegmentationSuperAlpide::flatToCurved(float xFlat, float yFlat, float& xCurved, float& yCurved)
{
  float phi = xFlat / (mRadii[mLayer] + SegmentationSuperAlpide::mSensorLayerThickness / 2);
  float dist = yFlat + (mRadii[mLayer] + SegmentationSuperAlpide::mSensorLayerThickness / 2);
  float tang = std::tan((double)constants::math::PI / 2 - (double)phi);
  xCurved = (xFlat > 0 ? 1.f : -1.f) * dist / std::sqrt(1 + tang * tang);
  yCurved = xCurved * tang;
}

inline void SegmentationSuperAlpide::localToDetectorUnchecked(float xRow, float zCol, int& iRow, int& iCol)
{
  // convert to row/col w/o over/underflow check
  xRow = 0.5 * (mActiveMatrixSizeRows - mPassiveEdgeTop + mPassiveEdgeReadOut) - xRow; // coordinate wrt top edge of Active matrix
  zCol += 0.5 * mActiveMatrixSizeCols;                                                 // coordinate wrt left edge of Active matrix
  iRow = int(xRow / mPitchRow);
  iCol = int(zCol / mPitchCol);
  if (xRow < 0) {
    iRow -= 1;
  }
  if (zCol < 0) {
    iCol -= 1;
  }
}

inline bool SegmentationSuperAlpide::localToDetector(float xRow, float zCol, int& iRow, int& iCol)
{
  // convert to row/col
  xRow = 0.5 * (mActiveMatrixSizeRows - mPassiveEdgeTop + mPassiveEdgeReadOut) - xRow; // coordinate wrt left edge of Active matrix
  zCol += 0.5 * mActiveMatrixSizeCols;                                                 // coordinate wrt bottom edge of Active matrix
  if (xRow < 0 || xRow >= mActiveMatrixSizeRows || zCol < 0 || zCol >= mActiveMatrixSizeCols) {
    iRow = iCol = -1;
    return false;
  }
  iRow = int(xRow / mPitchRow);
  iCol = int(zCol / mPitchCol);
  return true;
}

inline void SegmentationSuperAlpide::detectorToLocalUnchecked(int iRow, int iCol, float& xRow, float& zCol)
{
  xRow = getFirstRowCoordinate() - iRow * mPitchRow;
  zCol = iCol * mPitchCol + getFirstColCoordinate();
}

inline void SegmentationSuperAlpide::detectorToLocalUnchecked(float row, float col, float& xRow, float& zCol)
{
  xRow = getFirstRowCoordinate() - row * mPitchRow;
  zCol = col * mPitchCol + getFirstColCoordinate();
}

inline void SegmentationSuperAlpide::detectorToLocalUnchecked(float row, float col, math_utils::Point3D<float>& loc)
{
  loc.SetCoordinates(getFirstRowCoordinate() - row * mPitchRow, 0.f, col * mPitchCol + getFirstColCoordinate());
}

inline bool SegmentationSuperAlpide::detectorToLocal(int iRow, int iCol, float& xRow, float& zCol)
{
  if (iRow < 0 || iRow >= mNRows || iCol < 0 || iCol >= mNCols) {
    return false;
  }
  detectorToLocalUnchecked(iRow, iCol, xRow, zCol);
  return true;
}

inline bool SegmentationSuperAlpide::detectorToLocal(float row, float col, float& xRow, float& zCol)
{
  if (row < 0 || row >= mNRows || col < 0 || col >= mNCols) {
    return false;
  }
  detectorToLocalUnchecked(row, col, xRow, zCol);
  return true;
}

inline bool SegmentationSuperAlpide::detectorToLocal(float row, float col, math_utils::Point3D<float>& loc)
{
  if (row < 0 || row >= mNRows || col < 0 || col >= mNCols) {
    return false;
  }
  detectorToLocalUnchecked(row, col, loc);
  return true;
}
} // namespace its3
} // namespace o2

#endif
