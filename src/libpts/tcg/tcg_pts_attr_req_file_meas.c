/*
 * Copyright (C) 2011-2012 Sansar Choinyambuu, Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "tcg_pts_attr_req_file_meas.h"

#include <pa_tnc/pa_tnc_msg.h>
#include <bio/bio_writer.h>
#include <bio/bio_reader.h>
#include <utils/debug.h>

typedef struct private_tcg_pts_attr_req_file_meas_t private_tcg_pts_attr_req_file_meas_t;

/**
 * Request File Measurement
 * see section 3.19.1 of PTS Protocol: Binding to TNC IF-M Specification
 *
 *					   1				   2				   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |	 Flags	 |   Reserved	|		  Request ID				|
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |						   Delimiter							|
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  ~	   Fully Qualified File Pathname (Variable Length)			~
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define PTS_REQ_FILE_MEAS_SIZE			8
#define PTS_REQ_FILE_MEAS_RESERVED		0x00
#define PTS_REQ_FILE_MEAS_NO_FLAGS		0x00

#define DIRECTORY_CONTENTS_FLAG			(1<<7)

/**
 * Private data of an tcg_pts_attr_req_file_meas_t object.
 */
struct private_tcg_pts_attr_req_file_meas_t {

	/**
	 * Public members of tcg_pts_attr_req_file_meas_t
	 */
	tcg_pts_attr_req_file_meas_t public;

	/**
	 * Vendor-specific attribute type
	 */
	pen_type_t type;

	/**
	 * Attribute value
	 */
	chunk_t value;

	/**
	 * Noskip flag
	 */
	bool noskip_flag;

	/**
	 * Directory Contents flag
	 */
	bool directory_flag;

	/**
	 * Request ID
	 */
	u_int16_t request_id;

	/**
	 * UTF8 Encoding of Delimiter Character
	 */
	u_int32_t delimiter;

	/**
	 * Fully Qualified File Pathname
	 */
	char *pathname;

	/**
	 * Reference count
	 */
	refcount_t ref;
};

METHOD(pa_tnc_attr_t, get_type, pen_type_t,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	return this->type;
}

METHOD(pa_tnc_attr_t, get_value, chunk_t,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	return this->value;
}

METHOD(pa_tnc_attr_t, get_noskip_flag, bool,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	return this->noskip_flag;
}

METHOD(pa_tnc_attr_t, set_noskip_flag,void,
	private_tcg_pts_attr_req_file_meas_t *this, bool noskip)
{
	this->noskip_flag = noskip;
}

METHOD(pa_tnc_attr_t, build, void,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	u_int8_t flags = PTS_REQ_FILE_MEAS_NO_FLAGS;
	chunk_t pathname;
	bio_writer_t *writer;

	if (this->value.ptr)
	{
		return;
	}
	if (this->directory_flag)
	{
		flags |= DIRECTORY_CONTENTS_FLAG;
	}
	pathname = chunk_create(this->pathname, strlen(this->pathname));

	writer = bio_writer_create(PTS_REQ_FILE_MEAS_SIZE);
	writer->write_uint8 (writer, flags);
	writer->write_uint8 (writer, PTS_REQ_FILE_MEAS_RESERVED);
	writer->write_uint16(writer, this->request_id);
	writer->write_uint32(writer, this->delimiter);
	writer->write_data  (writer, pathname);
	this->value = chunk_clone(writer->get_buf(writer));
	writer->destroy(writer);
}

METHOD(pa_tnc_attr_t, process, status_t,
	private_tcg_pts_attr_req_file_meas_t *this, u_int32_t *offset)
{
	bio_reader_t *reader;
	u_int8_t flags;
	u_int8_t reserved;
	chunk_t pathname;

	if (this->value.len < PTS_REQ_FILE_MEAS_SIZE)
	{
		DBG1(DBG_TNC, "insufficient data for Request File Measurement");
		*offset = 0;
		return FAILED;
	}

	reader = bio_reader_create(this->value);
	reader->read_uint8 (reader, &flags);
	reader->read_uint8 (reader, &reserved);
	reader->read_uint16(reader, &this->request_id);
	reader->read_uint32(reader, &this->delimiter);
	reader->read_data  (reader, reader->remaining(reader), &pathname);

	this->directory_flag = (flags & DIRECTORY_CONTENTS_FLAG) !=
							PTS_REQ_FILE_MEAS_NO_FLAGS;

	this->pathname = malloc(pathname.len + 1);
	memcpy(this->pathname, pathname.ptr, pathname.len);
	this->pathname[pathname.len] = '\0';

	reader->destroy(reader);
	return SUCCESS;
}

METHOD(pa_tnc_attr_t, get_ref, pa_tnc_attr_t*,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	ref_get(&this->ref);
	return &this->public.pa_tnc_attribute;
}

METHOD(pa_tnc_attr_t, destroy, void,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	if (ref_put(&this->ref))
	{
		free(this->pathname);
		free(this->value.ptr);
		free(this);
	}
}

METHOD(tcg_pts_attr_req_file_meas_t, get_directory_flag, bool,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	return this->directory_flag;
}

METHOD(tcg_pts_attr_req_file_meas_t, get_request_id, u_int16_t,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	return this->request_id;
}

METHOD(tcg_pts_attr_req_file_meas_t, get_delimiter, u_int32_t,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	return this->delimiter;
}

METHOD(tcg_pts_attr_req_file_meas_t, get_pathname, char*,
	private_tcg_pts_attr_req_file_meas_t *this)
{
	return this->pathname;
}

/**
 * Described in header.
 */
pa_tnc_attr_t *tcg_pts_attr_req_file_meas_create(bool directory_flag,
												 u_int16_t request_id,
												 u_int32_t delimiter,
												 char *pathname)
{
	private_tcg_pts_attr_req_file_meas_t *this;

	INIT(this,
		.public = {
			.pa_tnc_attribute = {
				.get_type = _get_type,
				.get_value = _get_value,
				.get_noskip_flag = _get_noskip_flag,
				.set_noskip_flag = _set_noskip_flag,
				.build = _build,
				.process = _process,
				.get_ref = _get_ref,
				.destroy = _destroy,
			},
			.get_directory_flag = _get_directory_flag,
			.get_request_id = _get_request_id,
			.get_delimiter = _get_delimiter,
			.get_pathname = _get_pathname,
		},
		.type = { PEN_TCG, TCG_PTS_REQ_FILE_MEAS },
		.directory_flag = directory_flag,
		.request_id = request_id,
		.delimiter = delimiter,
		.pathname = strdup(pathname),
		.ref = 1,
	);

	return &this->public.pa_tnc_attribute;
}


/**
 * Described in header.
 */
pa_tnc_attr_t *tcg_pts_attr_req_file_meas_create_from_data(chunk_t data)
{
	private_tcg_pts_attr_req_file_meas_t *this;

	INIT(this,
		.public = {
			.pa_tnc_attribute = {
				.get_type = _get_type,
				.get_value = _get_value,
				.get_noskip_flag = _get_noskip_flag,
				.set_noskip_flag = _set_noskip_flag,
				.build = _build,
				.process = _process,
				.get_ref = _get_ref,
				.destroy = _destroy,
			},
			.get_directory_flag = _get_directory_flag,
			.get_request_id = _get_request_id,
			.get_delimiter = _get_delimiter,
			.get_pathname = _get_pathname,
		},
		.type = { PEN_TCG, TCG_PTS_REQ_FILE_MEAS },
		.value = chunk_clone(data),
		.ref = 1,
	);

	return &this->public.pa_tnc_attribute;
}
