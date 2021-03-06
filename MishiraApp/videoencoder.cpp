//*****************************************************************************
// Mishira: An audiovisual production tool for broadcasting live video
//
// Copyright (C) 2014 Lucas Murray <lucas@polyflare.com>
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//*****************************************************************************

#include "videoencoder.h"
#include "profile.h"

const QString LOG_CAT = QStringLiteral("Video");

//=============================================================================
// Serialization helpers

// These helpers exist so that we do not depend on the order of enums in our
// serialization and deserialization methods. This is important as if the order
// changes in a future release then we will be unable to deserialize older
// versions.
// WARNING: ALL CHANGES MADE TO THESE METHODS MUST BE BACKWARDS-COMPATIBLE!

static quint32 scalingToInt32(SclrScalingMode scaling)
{
	switch(scaling) {
	case SclrStretchScale: return 0;
	default:
	case SclrSnapToInnerScale: return 1;
	case SclrSnapToOuterScale: return 2;
	}
	return 1; // Should never be reached
}

static SclrScalingMode int32ToScaling(quint32 scaling)
{
	switch(scaling) {
	case 0: return SclrStretchScale;
	default:
	case 1: return SclrSnapToInnerScale;
	case 2: return SclrSnapToOuterScale;
	}
	return SclrSnapToInnerScale; // Should never be reached
}

static quint32 filterToInt32(VidgfxFilter filter)
{
	switch(filter) {
	case GfxPointFilter: return 0;
	default:
	case GfxBilinearFilter: return 1;
	}
	return 1; // Should never be reached
}

static VidgfxFilter int32ToFilter(quint32 filter)
{
	switch(filter) {
	case 0: return GfxPointFilter;
	default:
	case 1: return GfxBilinearFilter;
	}
	return GfxBilinearFilter; // Should never be reached
}

//=============================================================================
// VideoEncoder class

VideoEncoder::VideoEncoder(
	Profile *profile, VencType type, QSize size, SclrScalingMode scaling,
	VidgfxFilter scaleFilter, Fraction framerate)
	: m_profile(profile)
	, m_type(type)
	, m_ref(0)
	, m_isRunning(false)
	, m_size(size)
	, m_scaling(scaling)
	, m_scaleFilter(scaleFilter)
	, m_framerate(framerate.reduced())
{
}

VideoEncoder::~VideoEncoder()
{
	if(m_ref != 0) {
		appLog(LOG_CAT, Log::Warning) <<
			QStringLiteral("Destroying referenced video encoder %1 (%L2 references)")
			.arg(getId())
			.arg(m_ref);
	}
}

quint32 VideoEncoder::getId()
{
	if(m_profile == NULL)
		return 0;
	return m_profile->idOfVideoEncoder(this);
}

/// <summary>
/// Reference this video encoder. If an encoder has one or more references it
/// activates and begins encoding frames.
/// </summary>
/// <returns>True if the encoder successfully activated</returns>
bool VideoEncoder::refActivate()
{
	m_ref++;
	if(m_ref == 1) {
		if(!initializeEncoder()) {
			m_ref--;
			return false;
		}
	}
	return true;
}

void VideoEncoder::derefActivate()
{
	if(m_ref <= 0) {
		appLog(LOG_CAT, Log::Warning) <<
			QStringLiteral("Video encoder %1 was dereferenced more times than it was referenced")
			.arg(getId());
		return;
	}
	m_ref--;
	if(m_ref == 0)
		shutdownEncoder(); // TODO: Flush?
}

void VideoEncoder::serialize(QDataStream *stream) const
{
	// Write data version number
	*stream << (quint32)0;

	// Save our data
	*stream << m_size;
	*stream << scalingToInt32(m_scaling);
	*stream << filterToInt32(m_scaleFilter);
	// We don't store framerate as it's always equal to the profile's
}

bool VideoEncoder::unserialize(QDataStream *stream)
{
	quint32	uint32Data;

	// TODO: Make sure we cannot unserialize when the encoder is in use

	// Load defaults here if ever needed

	// Read data version number
	quint32 version;
	*stream >> version;
	if(version == 0) {
		*stream >> m_size;
		*stream >> uint32Data;
		m_scaling = int32ToScaling(uint32Data);
		*stream >> uint32Data;
		m_scaleFilter = int32ToFilter(uint32Data);
		// We assume that `m_framerate` is already properly set
	} else {
		appLog(LOG_CAT, Log::Warning)
			<< "Unknown version number in video encoder serialized data, "
			<< "cannot load settings";
		return false;
	}

	return true;
}
