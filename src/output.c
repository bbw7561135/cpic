#define _GNU_SOURCE
#include "output.h"

#include <linux/limits.h>
#include <hdf5.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#define DEBUG 0
#include "log.h"
#include "def.h"
#include "utils.h"

#define H5X 1
#define H5Y 0

#define ALIGNED
#define IS_ALIGNED(ADDR, SIZE) (((uintptr_t)(const void *)(ADDR)) % (SIZE) == 0)

int
output_init(sim_t *sim, output_t *out)
{
	const char *path;

	if(config_lookup_string(sim->conf, "output.path", &path) != CONFIG_TRUE)
	{
		err("No output path specified, output will not be saved\n");
		out->enabled = 0;
		return 0;
	}

	strncpy(out->path, path, PATH_MAX-1);
	out->path[PATH_MAX-1] = '\0';

	out->enabled = 1;

	if(config_lookup_int(sim->conf, "output.period.field",
				&out->period_field) != CONFIG_TRUE)
	{
		err("Using default period for fields of 1 sample/iteration\n");
		out->period_field = 1;
	}

	if(config_lookup_int(sim->conf, "output.period.particle",
				&out->period_particle) != CONFIG_TRUE)
	{
		err("Using default period for particles of 1 sample/iteration\n");
		out->period_particle = 1;
	}

	if(config_lookup_int(sim->conf, "output.slices",
				&out->max_slices) != CONFIG_TRUE)
	{
		err("Using one slice for field output\n");
		out->max_slices = 1;
	}

	if(config_lookup_int64(sim->conf, "output.alignment",
				&out->alignment) != CONFIG_TRUE)
	{
		err("Using default 512 bytes for alignment\n");
		out->alignment = 512;
	}

	if(sim->blocksize[Y] % out->max_slices)
	{
		err("The number of slices %d doesn't divide the blocksize in Y of %d\n",
				out->max_slices, sim->blocksize[Y]);
		return 1;

	}

	return 0;
}

int
write_xdmf_specie(sim_t *sim, int np)
{
	FILE *f;
	char file[PATH_MAX];
	char dataset[PATH_MAX];

	/* TODO: Multiple species */

	if(snprintf(file, PATH_MAX, "%s/specie0-iter%d.xdmf",
			sim->output->path, sim->iter) >= PATH_MAX)
	{
		return -1;
	}

	if(snprintf(dataset, PATH_MAX, "specie0-iter%d.h5",
			sim->iter) >= PATH_MAX)
	{
		return -1;
	}


	f = fopen(file, "w");

	fprintf(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	fprintf(f, "<Xdmf xmlns:xi=\"http://www.w3.org/2001/XInclude\" Version=\"3.0\">\n");
	fprintf(f, "  <Domain>\n");
	fprintf(f, "    <Grid Name=\"particles\">\n");
	fprintf(f, "      <Topology TopologyType=\"Polyvertex\"/>\n");
	fprintf(f, "      <Geometry Origin=\"\" Type=\"XY\">\n");
	fprintf(f, "        <DataItem Dimensions=\"%d %d\" DataType=\"Float\" Precision=\"8\" Format=\"HDF\">%s:/xy</DataItem>\n",
			np, 2, dataset);
	fprintf(f, "      </Geometry>\n");
	fprintf(f, "      <Attribute Center=\"Node\" Name=\"ID\" DataType=\"Scalar\">\n");
	fprintf(f, "        <DataItem Dimensions=\"%d %d\" DataType=\"Int\" Precision=\"4\" Format=\"HDF\">%s:/id</DataItem>\n",
			np, 1, dataset);
	fprintf(f, "      </Attribute>\n");
	fprintf(f, "      <Attribute Center=\"Node\" Name=\"U\" DataType=\"Scalar\">\n");
	fprintf(f, "        <DataItem Dimensions=\"%d %d\" DataType=\"Float\" Precision=\"8\" Format=\"HDF\">%s:/u</DataItem>\n",
			np, 2, dataset);
	fprintf(f, "      </Attribute>\n");
	fprintf(f, "      <Attribute Center=\"Node\" Name=\"U_MAG\" DataType=\"Scalar\">\n");
	fprintf(f, "        <DataItem Dimensions=\"%d %d\" DataType=\"Float\" Precision=\"8\" Format=\"HDF\">%s:/u_mag</DataItem>\n",
			np, 1, dataset);
	fprintf(f, "      </Attribute>\n");
	fprintf(f, "    </Grid>\n");
	fprintf(f, "  </Domain>\n");
	fprintf(f, "</Xdmf>\n");

	fclose(f);

	return 0;
}
int
output_chunk(sim_t *sim, int ic, int *acc_np, hid_t file_id)
{
	int ip;
	char file[PATH_MAX];
	particle_set_t *set;
	plasma_chunk_t *chunk;
	particle_t *p;
	double *position, *u, *u_mag;
	int *id;
	int np;

	hid_t dataset, dataspace;
	herr_t status;
	hsize_t dims[2];

	chunk = &sim->plasma.chunks[ic];

	/* TODO: Multiple species */
	set = &chunk->species[0];

	np = set->nparticles;

	position = safe_malloc(np*2*sizeof(double));
	u = safe_malloc(np*2*sizeof(double));
	u_mag = safe_malloc(np*sizeof(double));
	id = safe_malloc(np*sizeof(int));

	/* TODO: We need to ensure this serialization is not a bottle-neck */
	for(ip = 0, p=set->particles; ip < np; ip++, p=p->next)
	{
		position[ip*2 + X] = p->x[X];
		position[ip*2 + Y] = p->x[Y];
		u[ip*2 + X] = p->u[X];
		u[ip*2 + Y] = p->u[Y];
		u_mag[ip] = sqrt(p->u[X] * p->u[X] + p->u[Y] * p->u[Y]);

		dbg("Saving ip=%d p%d at (%e,%e) as (%e,%e)\n",
				ip, p->i, p->x[X], p->x[Y],
				position[ip*2 + X], position[ip*2 + Y]);
		id[ip] = p->i;
	}

	usleep(10000 + (rand() % 50000));
	err("Writing %d particles for chunk %d\n", np, ic);

	/* We need to write the particles after the previous ones */

	hsize_t dim[2];
	hsize_t chunk_dim[2];
	hsize_t offset[2];
	hsize_t count[2];

	hid_t disk_ds1d, disk_ds2d;
	hid_t mem_ds1d, mem_ds2d;

	/* The dimensions of the complete dataset, with all chunks */
	dim[0] = sim->species[0].nparticles;
	dim[1] = 2;

	/* Dimensions of the chunk */
	chunk_dim[0] = np;
	chunk_dim[1] = dim[1];

	/* Create dataspaces */
	disk_ds1d = H5Screate_simple(1, dim, NULL);
	disk_ds2d = H5Screate_simple(2, dim, NULL);
	mem_ds1d = H5Screate_simple(1, chunk_dim, NULL);
	mem_ds2d = H5Screate_simple(2, chunk_dim, NULL);

	/* Advance to pass the previous chunks */
	offset[0] = *acc_np;
	offset[1] = 0;

	/* Select the proper chunk place in the target dataset */
	status = H5Sselect_hyperslab(disk_ds2d, H5S_SELECT_SET, offset, NULL, chunk_dim, NULL);
	status = H5Sselect_hyperslab(disk_ds1d, H5S_SELECT_SET, offset, NULL, chunk_dim, NULL);


	/* The same in memory, without offset */
	offset[0] = 0;
	status = H5Sselect_hyperslab(mem_ds2d, H5S_SELECT_SET, offset, NULL, chunk_dim, NULL);
	status = H5Sselect_hyperslab(mem_ds1d, H5S_SELECT_SET, offset, NULL, chunk_dim, NULL);

	/* Now we are ready to write */

	dataset = H5Dopen1(file_id, "/xy");
	status = H5Dwrite(dataset, H5T_NATIVE_DOUBLE, mem_ds2d, disk_ds2d,
			H5P_DEFAULT, position);
	status = H5Dclose(dataset);

	dataset = H5Dopen1(file_id, "/u");
	status = H5Dwrite(dataset, H5T_NATIVE_DOUBLE, mem_ds2d, disk_ds2d,
			H5P_DEFAULT, u);
	status = H5Dclose(dataset);

	dataset = H5Dopen1(file_id, "/u_mag");
	status = H5Dwrite(dataset, H5T_NATIVE_DOUBLE, mem_ds1d, disk_ds1d,
			H5P_DEFAULT, u_mag);
	status = H5Dclose(dataset);

	dataset = H5Dopen1(file_id, "/id");
	status = H5Dwrite(dataset, H5T_NATIVE_INT, mem_ds1d, disk_ds1d,
			H5P_DEFAULT, id);
	status = H5Dclose(dataset);

	if(status)
		return -1;

	*acc_np += np;

	free(position);
	free(u);
	free(id);

	H5Sclose(disk_ds1d);
	H5Sclose(disk_ds2d);
	H5Sclose(mem_ds1d);
	H5Sclose(mem_ds2d);

	return 0;
}

int
specie_create_datasets(sim_t *sim, hid_t fid)
{
	hsize_t dim[2];
	int np;
	hid_t dataset, ds1d, ds2d;
	herr_t status;
	hid_t ND = H5T_NATIVE_DOUBLE;
	hid_t NI = H5T_NATIVE_INT;

	/* TODO: Support multiple species */

	/* Create datasets with proper sizes */
	np = sim->species[0].nparticles;
	dim[0] = np;
	dim[1] = 2;

	ds1d = H5Screate_simple(1, dim, NULL);
	ds2d = H5Screate_simple(2, dim, NULL);

	dataset = H5Dcreate1(fid, "/id", NI, ds1d, H5P_DEFAULT);
	status = H5Dclose(dataset);

	dataset = H5Dcreate1(fid, "/xy", ND, ds2d, H5P_DEFAULT);
	status = H5Dclose(dataset);

	dataset = H5Dcreate1(fid, "/u", ND, ds2d, H5P_DEFAULT);
	status = H5Dclose(dataset);

	dataset = H5Dcreate1(fid, "/u_mag", ND, ds1d, H5P_DEFAULT);
	status = H5Dclose(dataset);

	if(status)
		return -1;

	H5Sclose(ds1d);
	H5Sclose(ds2d);

	return 0;
}

int
output_particles(sim_t *sim)
{
	char file[PATH_MAX];
	int ic;
	int acc_np;
	int all_np;
	hid_t file_id;
	herr_t status;

	perf_start(&sim->timers[TIMER_OUTPUT_PARTICLES]);

	/* Count total number of particles */

	if(snprintf(file, PATH_MAX, "%s/specie0-iter%d.h5",
			sim->output->path, sim->iter) >= PATH_MAX)
	{
		return -1;
	}

	all_np = sim->species[0].nparticles;
	write_xdmf_specie(sim, all_np);


	/* Create a new file using default properties. */
	file_id = H5Fcreate(file, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

	specie_create_datasets(sim, file_id);

	acc_np = 0;
	for(ic=0; ic<32; ic++)
	{
		//#pragma oss task firstprivate(ic)
		//{
		//	sleep(100000 + (rand() % 1000000));
		//	printf("ic=%d\n", ic);
		//}
		output_chunk(sim, ic, &acc_np, file_id);
	}

	/* Close the file. */
	#pragma oss taskwait
	status = H5Fclose(file_id);
	if(status)
		return -1;

	perf_stop(&sim->timers[TIMER_OUTPUT_PARTICLES]);
	return 0;
}

int
write_xdmf_fields(sim_t *sim)
{
	FILE *f;
	char file[PATH_MAX];
	char dataset[PATH_MAX];
	mat_t *rho, *phi;

	rho = sim->field.rho;
	phi = sim->field.phi;

	if(snprintf(file, PATH_MAX, "%s/fields-iter%d.xdmf",
			sim->output->path, sim->iter) >= PATH_MAX)
	{
		return -1;
	}

	if(snprintf(dataset, PATH_MAX, "fields-iter%d.h5",
			sim->iter) >= PATH_MAX)
	{
		return -1;
	}

	f = fopen(file, "w");

	fprintf(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	fprintf(f, "<Xdmf xmlns:xi=\"http://www.w3.org/2001/XInclude\" Version=\"3.0\">\n");
	fprintf(f, "  <Domain>\n");
	fprintf(f, "    <Grid Name=\"fields\">\n");
	fprintf(f, "      <Topology TopologyType=\"3DCoRectMesh\" NumberOfElements=\"%d %d %d\"/>\n",
			1, rho->shape[Y], rho->shape[X]);
	fprintf(f, "      <Geometry Origin=\"\" Type=\"ORIGIN_DXDYDZ\">\n");
	fprintf(f, "        <DataItem Format=\"XML\" Dimensions=\"3\">\n");
	fprintf(f, "            0.0 0.0 0.0\n");
	fprintf(f, "        </DataItem>\n");
	fprintf(f, "        <DataItem Format=\"XML\" Dimensions=\"3\">\n");
	fprintf(f, "            %f %f %f\n", sim->dx[Z], sim->dx[Y], sim->dx[X]);
	fprintf(f, "        </DataItem>\n");
	fprintf(f, "      </Geometry>\n");
	fprintf(f, "      <Attribute Center=\"Node\" Name=\"RHO\" DataType=\"Scalar\">\n");
	fprintf(f, "        <DataItem Dimensions=\"%d %d %d\" DataType=\"Float\" Precision=\"8\" Format=\"HDF\">%s:/rho</DataItem>\n",
			1, rho->shape[Y], rho->shape[X], dataset);
	fprintf(f, "      </Attribute>\n");
	fprintf(f, "      <Attribute Center=\"Node\" Name=\"PHI\" DataType=\"Scalar\">\n");
	fprintf(f, "        <DataItem Dimensions=\"%d %d %d\" DataType=\"Float\" Precision=\"8\" Format=\"HDF\">%s:/phi</DataItem>\n",
			1, phi->shape[Y], phi->shape[X], dataset);
	fprintf(f, "      </Attribute>\n");
	fprintf(f, "    </Grid>\n");
	fprintf(f, "  </Domain>\n");
	fprintf(f, "</Xdmf>\n");

	fclose(f);

	return 0;
}

//int
//output_slice(sim_t *sim, mat_t *slice, int slice_i, hid_t src, hid_t dst)
//{
//	count[H5X] = 1;
//	count[H5Y] = 1;
//	size[H5X] = slice->shape[X];
//	size[H5Y] = slice->shape[Y];
//
//	/* Otherwise we must offset the field by the current process rank */
//	assert(sim->nprocs == 1);
//
//	/* First select the target region of the disk to be written */
//
//	offset[H5X] = 0;
//	offset[H5Y] = slice->shape[Y] * slice_i;
//
//	dbg("Diskspace offset (%d %d) size (%d %d)\n",
//			offset[X], offset[Y], size[X], size[Y]);
//
//	status = H5Sselect_hyperslab(dst, H5S_SELECT_SET, offset, NULL,
//			count, size);
//
//	/* Then a memspace referring to the memory chunk */
//	rho_dim[H5X] = slice->real_shape[X];
//	rho_dim[H5Y] = slice->shape[Y];
//	offset[H5X] = 0;
//	offset[H5Y] = 0;
//
//	/* size and offset go unchanged */
//	status = H5Sselect_hyperslab(src, H5S_SELECT_SET, offset, NULL,
//			count, size);
//
//	/* Notice that the padding for the FFT in the +X side of rho should be
//	 * skipped */
//	status = H5Dwrite(dataset, H5T_NATIVE_DOUBLE, src, dst,
//			H5P_DEFAULT, rho->data);
//
//	return 0;
//}

int
write_vector(int fd, size_t offset, double *vector, size_t size, size_t alignment)
{
	ssize_t ret;
	uintptr_t p;

	ret = 0;

	assert(IS_ALIGNED(offset, alignment));
	assert(IS_ALIGNED(vector, alignment));
	assert(IS_ALIGNED(size, alignment));

	dbg("Writing %lu bytes (%lu blocks) at offset %lu (%lu blocks)\n",
			size, size/alignment, offset, offset/alignment);

	while((ret = pwrite(fd, vector, size, offset)) != size)
	{
		dbg("pwrite wrote %zd bytes of %zu, errno=%d\n", ret, size, errno);
		if(ret < 0)
		{
			perror("pwrite");
			return -1;
		}

		size -= ret;
		offset += ret;
		/* FIXME:
		 * (char*)vector+=ret */
		return -1;
	}

	return 0;
}

int
write_field(sim_t *sim, output_t *out, mat_t *m, const char *name)
{

	/* In order to write the field, we must divide the matrix in parts. As
	 * the padding is hard to remove without affecting the alignment, we
	 * choose to write the fields as-is into disk.
	 *
	 * The number of parts will then vary as the size of the fields. */

	int i, fd, ret;
	size_t n, nlast, offset, total_bytes, total_blocks, bsize;
	size_t nblocks, slice_bytes, left;
	char *data;
	char file[PATH_MAX];

	ret = 0;

	if(snprintf(file, PATH_MAX, "%s/%d/%s.bin",
				out->path, sim->iter, name) >= PATH_MAX)
	{
		err("Path exceeds PATH_MAX\n");
		return -1;
	}

	dbg("Writing to %s\n", file);

	fd = open(file, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_DIRECT | O_NOATIME,
			S_IRUSR | S_IWUSR);

	if(fd < 0)
	{
		perror("open");
		return -1;
	}

	/* If m is a view, the size is -1 and we should abort */
	assert(m->size > 0);

	bsize = out->alignment;
	total_bytes = m->aligned_size;
	data = (char *) m->data;
	total_blocks = (m->size + (bsize-1)) / bsize;

	nblocks = total_blocks / out->max_slices;
	left = total_blocks % out->max_slices;
	slice_bytes = nblocks * bsize;
	offset = 0;

	dbg("total_bytes=%zu bsize=%zu\n",
			total_bytes, bsize);
	dbg("total_blocks=%zu nblocks=%zu slice_bytes=%zu\n",
			total_blocks, nblocks, slice_bytes);

	for(i=0; i<out->max_slices; i++)
	{
		n = slice_bytes;
		if(left > 0)
		{
			n += bsize;
			left--;
		}

		dbg("Writing to size=%zu (%zu blocks)\n",
				n, n/bsize);

		if(!(data+offset+n <= data+total_bytes))
		{
			err("WRITING OUT OF BOUNDS: last written %p, last allocated %p\n",
					data+offset+n, data+total_bytes);
			abort();
		}

		#pragma oss task
		if((ret = write_vector(fd, offset, (double *) (data+offset), n, bsize)))
			abort();
			/* Goto is not working with tasks */
			//goto err;

		offset += n;
	}

	//nlast = total_blocks % (out->max_slices - 1);
	/* We still have some elements left, which are not multiple of the block
	 * size, but they are aligned */
	//if((ret = write_vector(fd, offset, &m->data[offset_elem], left * sizeof(double))))
	//{
	//	goto err;
	//}

	//assert(offset_elem + left == total);

	#pragma oss taskwait

//err:
	close(fd);
	return ret;

}

int
output_fields(sim_t *sim)
{
	output_t *out;
	char dir[PATH_MAX];

	perf_start(&sim->timers[TIMER_OUTPUT_FIELDS]);

	out = sim->output;

	if(snprintf(dir, PATH_MAX, "%s/%d",
				out->path, sim->iter) >= PATH_MAX)
	{
		err("Dir path exceeds PATH_MAX\n");
		return -1;
	}

	if(mkdir(dir, 0700) && errno != EEXIST)
	{
		perror("mkdir");
		return -1;
	}

	if(write_field(sim, sim->output, sim->field._rho, "rho"))
	{
		err("write_field failed\n");
		return -1;
	}

	if(write_field(sim, sim->output, sim->field._phi, "phi"))
	{
		err("write_field failed\n");
		return -1;
	}

	perf_stop(&sim->timers[TIMER_OUTPUT_FIELDS]);

//	char file[PATH_MAX];
//	mat_t *rho, /**_rho,*/ *phi, *_phi, *slice;
//	int i, ix, iy;
//	//int slice_shape[MAX_DIM];
//	//output_t *out;
//
//	hid_t file_id, dataset, dataspace, memspace;
//	herr_t status;
//	hsize_t dims[2];
//	hsize_t rho_dim[2];
//	hsize_t phi_dim[2];
//	hsize_t offset[2];
//	hsize_t count[2];
//	hsize_t size[2];
//
//
//	//out = sim->output;
//
//	write_xdmf_fields(sim);
//
//	if(snprintf(file, PATH_MAX, "%s/fields-iter%d.h5",
//			sim->output->path, sim->iter) >= PATH_MAX)
//	{
//		return -1;
//	}
//
//	rho = sim->field.rho;
//	//_rho = sim->field._rho;
//
//	//slice_shape[X] = sim->blocksize[X];
//	//slice_shape[Y] = sim->blocksize[Y] / out->max_slices;
//	//slice_shape[Z] = sim->blocksize[Z];
//
//	//for(i=0; i<out->max_slices; i++)
//	//{
//	//	slice = mat_view(rho, 0, slice_shape[Y] * i, slice_shape);
//	//	output_slice(sim, slice, i);
//	//	free(slice);
//	//}
//
//	/* Create a new file using default properties. */
//	file_id = H5Fcreate(file, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
//
//	/* We assume the rows start at 0 */
//	dims[H5X] = rho->shape[X];
//	dims[H5Y] = rho->shape[Y];
//
//	dataspace = H5Screate_simple(2, dims, NULL);
//	dataset = H5Dcreate1(file_id, "/rho", H5T_NATIVE_DOUBLE, dataspace,
//			H5P_DEFAULT);
//
//	offset[H5X] = 0;
//	offset[H5Y] = 0;
//	count[H5X] = 1;
//	count[H5Y] = 1;
//	size[H5X] = rho->shape[X];
//	size[H5Y] = rho->shape[Y];
//
//	dbg("Dataspace offset (%d %d) size (%d %d)\n",
//			offset[X], offset[Y], size[X], size[Y]);
//
//	status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL,
//			count, size);
//
//	/* Then a memspace referring to the memory chunk */
//	rho_dim[H5X] = rho->real_shape[X];
//	rho_dim[H5Y] = rho->shape[Y];
//	memspace = H5Screate_simple(2, rho_dim, NULL);
//
//	/* size and offset go unchanged */
//	status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, NULL,
//			count, size);
//
//	/* Notice that the padding for the FFT in the +X side of rho should be
//	 * skipped */
//	status = H5Dwrite(dataset, H5T_NATIVE_DOUBLE, memspace, dataspace,
//			H5P_DEFAULT, rho->data);
//	status = H5Dclose(dataset);
//
//	/* ------------------ PHI ------------------- */
//
//	phi = sim->field.phi;
//	_phi = sim->field._phi;
//
//	ix = _phi->shape[X] - 2;
//	/* Erase the FFT padding */
//	for(iy=2; iy<phi->shape[Y]-1; iy++)
//	{
//		MAT_XY(_phi, ix, iy) = NAN;
//		MAT_XY(_phi, ix+1, iy) = NAN;
//	}
//
//	/* We assume the rows start at 0 */
//	dims[H5X] = phi->shape[X];
//	dims[H5Y] = phi->shape[Y];
//
//	dataspace = H5Screate_simple(2, dims, NULL);
//	dataset = H5Dcreate1(file_id, "/phi", H5T_NATIVE_DOUBLE, dataspace,
//			H5P_DEFAULT);
//
//	offset[H5X] = 0;
//	offset[H5Y] = 0;
//	count[H5X] = 1;
//	count[H5Y] = 1;
//	size[H5X] = phi->shape[X];
//	size[H5Y] = phi->shape[Y];
//
//	status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL,
//			count, size);
//
//	/* Then a memspace referring to the memory chunk */
//	phi_dim[H5X] = phi->real_shape[X];
//	phi_dim[H5Y] = phi->shape[Y];
//	memspace = H5Screate_simple(2, phi_dim, NULL);
//
//	/* size and offset go unchanged */
//	status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, NULL,
//			count, size);
//
//	/* Notice that the padding for the FFT in the +X side of rho should be
//	 * skipped */
//	status = H5Dwrite(dataset, H5T_NATIVE_DOUBLE, memspace, dataspace,
//			H5P_DEFAULT, phi->data);
//	status = H5Dclose(dataset);
//
//
//	/* Close the file. */
//	status = H5Fclose(file_id);
//
//	if(status)
//		return -1;
//
//	perf_stop(&sim->timers[TIMER_OUTPUT_FIELDS]);

	return 0;
}
