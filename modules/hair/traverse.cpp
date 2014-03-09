//#undef NDEBUG

// hair
#include "hbvh.h"
// embree
#include "kernels/common/ray8.h"
#include "kernels/xeon/bvh4/bvh4.h"
#include "kernels/xeon/bvh4/bvh4_intersector8_hybrid.h"

#define REAL_INTERSECTION 1
#define STATS(a) 

namespace ospray {
  namespace hair {
    using namespace embree;

    long numIsecs = 0;
    long numIsecRecs = 0;
    long numHairletTravs = 0;

    struct SegStack {
      ssef v0,v1,v2,v3;
      int depth;
    };

    __forceinline 
    bool finalLineTest(const ssef &a, const ssef &b,
                       Ray8 &ray8,const size_t k)
    {
      const ssef minP = min(a,b);
      const ssef maxP = max(a,b);
      const ssef maxR = shuffle<3,3,3,3>(maxP);
      if (movemask((minP > maxR) | (maxP < -maxR)) & 0x3) return false;

      const float z = minP[2];
      if (z >= ray8.tfar[k]) return false;
      ray8.tfar[k] = z;
      ray8.Ng.x[k] = b[0] - a[0];
      ray8.Ng.y[k] = b[1] - a[1];
      ray8.Ng.z[k] = b[2] - a[2];
      return true;
    }

    __forceinline 
    bool finalSegmentTest(const ssef &v0,
                          const ssef &v1,
                          const ssef &v2,
                          const ssef &v3,
                          Ray8 &ray8,
                          const size_t k)
    {
      return 
        finalLineTest(v0,v1,ray8,k) |
        finalLineTest(v1,v2,ray8,k) |
        finalLineTest(v2,v3,ray8,k);
    }

    __forceinline 
    bool segmentTest(const ssef &v0,
                     const ssef &v1,
                     const ssef &v2,
                     const ssef &v3,
                     const Ray8 &ray8,
                     const size_t k)
    {
      const ssef min4 = min(v0,v1,v2,v3);
      const ssef max4 = max(v0,v1,v2,v3);
      const ssef maxR = shuffle<3,3,3,3>(max4);
      if (movemask((min4 > maxR) | (max4 < -maxR)) & 3) return false;
      if (min4[2] > ray8.tfar[k]) return false;
      return true;
    }

__noinline
//__forceinline
    bool intersectSegment1(const LinearSpace3<vec3fa> &space,
                           const vec3fa &org,
                           const Hairlet *hl, //const BVH4* bvh, 
                           const uint32 startVertex,
                           const size_t k,
                           Ray8 &ray)
    {
      STATS(numIsecs++);
      const Segment &seg = (const Segment &)hl->hg->vertex[startVertex];
      // const ssef _org = (const ssef &)org;
      
      const vec3fa _v0 = (const vec3fa &)seg.v0.pos - org;
      const vec3fa _v1 = (const vec3fa &)seg.v1.pos - org;
      const vec3fa _v2 = (const vec3fa &)seg.v2.pos - org;
      const vec3fa _v3 = (const vec3fa &)seg.v3.pos - org;
      
      const vec4f __v0(dot(_v0,space.vx),dot(_v0,space.vy),dot(_v0,space.vz),seg.v0.radius);
      const vec4f __v1(dot(_v1,space.vx),dot(_v1,space.vy),dot(_v1,space.vz),seg.v1.radius);
      const vec4f __v2(dot(_v2,space.vx),dot(_v2,space.vy),dot(_v2,space.vz),seg.v2.radius);
      const vec4f __v3(dot(_v3,space.vx),dot(_v3,space.vy),dot(_v3,space.vz),seg.v3.radius);

      ssef v0 = (const ssef &)__v0;
      ssef v1 = (const ssef &)__v1;
      ssef v2 = (const ssef &)__v2;
      ssef v3 = (const ssef &)__v3;
      int depth = 2;
      SegStack stack[20];
      SegStack *stackPtr = stack;
      const ssef half = 0.5f;
      bool foundHit = false;
      goto start;
      while (1) {
        if (stackPtr <= stack) break;
        --stackPtr;
        v0 = stackPtr->v0;
        v1 = stackPtr->v1;
        v2 = stackPtr->v2;
        v3 = stackPtr->v3;
        depth = stackPtr->depth;
      start:
        if (!segmentTest(v0,v1,v2,v3,ray,k))
          continue;

        const ssef v10 = half*(v0+v1);
        const ssef v11 = half*(v1+v2);
        const ssef v12 = half*(v2+v3);

        const ssef v20 = half*(v10+v11);
        const ssef v21 = half*(v11+v12);

        const ssef v30 = half*(v20+v21);
        if (depth == 0) {
          foundHit |= finalSegmentTest(v0,v10,v20,v30,ray,k);
          foundHit |= finalSegmentTest(v30,v21,v12,v3,ray,k);
          //   ray.primID[k] = (long(startVertex)*13)>>4;
          //   ray.geomID[k] = (long(startVertex)*13)>>4;

          //   vec3f N
          //     = space.vx * ray.Ng.x[k] 
          //     + space.vy * ray.Ng.y[k] 
          //     + space.vz * ray.Ng.z[k];
          // }
        } else {
          stackPtr[0].v0 = v3;
          stackPtr[0].v1 = v12;
          stackPtr[0].v2 = v21;
          stackPtr[0].v3 = v30;
          stackPtr[0].depth = depth-1;
          
          stackPtr ++;

          v0 = v0;
          v1 = v10;
          v2 = v20;
          v3 = v30;
          depth = depth-1;
          goto start;
        }
      }
      return foundHit;
    }

    __forceinline 
    void hairIntersect1(const int hairletID,
                        const Hairlet *hl, //const BVH4* bvh, 
                        // NodeRef root, 
                        const size_t k, 
                        Ray8& ray, 
                        const avx3f &ray_org, 
                        const avx3f &ray_dir, 
                        const avx3f &ray_rdir, 
                        const embree::LinearSpace3<avx3f> &ray_space
                        )
    {
      STATS(numHairletTravs++);
      static const int stackSizeSingle = 80;
      typedef int64 NodeRef;

      StackItemInt32<NodeRef> stack[stackSizeSingle];  //!< stack of nodes 
      StackItemInt32<NodeRef>* stackPtr = stack; // + 1;        //!< current stack pointer
      StackItemInt32<NodeRef>* stackEnd = stack + stackSizeSingle;

      uint mailbox[32];
      ((avxi*)mailbox)[0] = avxi(-1);
      ((avxi*)mailbox)[1] = avxi(-1);
      ((avxi*)mailbox)[2] = avxi(-1);
      ((avxi*)mailbox)[3] = avxi(-1);

      // bvh4 traversal...
      const sse3f org(ray_org.x[k], ray_org.y[k], ray_org.z[k]);
      const sse3f rdir(ray_rdir.x[k], ray_rdir.y[k], ray_rdir.z[k]);
      LinearSpace3<vec3fa> raySpace(vec3fa(ray_space.vx.x[k],
                                           ray_space.vx.y[k],
                                           ray_space.vx.z[k]),
                                    vec3fa(ray_space.vy.x[k],
                                           ray_space.vy.y[k],
                                           ray_space.vy.z[k]),
                                    vec3fa(ray_space.vz.x[k],
                                           ray_space.vz.y[k],
                                           ray_space.vz.z[k]));

      const vec3fa dir_k(ray_dir.x[k],ray_dir.y[k],ray_dir.z[k]);
      const vec3fa org_k(ray_org.x[k],ray_org.y[k],ray_org.z[k]);

      const sse3f org_rdir(org*rdir);
      const ssef  ray_t0(ray.tnear[k]);
      ssef  ray_t1(ray.tfar[k]);
      
      const ssef hl_bounds_min = *(ssef*)&hl->bounds.lower;
      const ssef hl_bounds_max = *(ssef*)&hl->bounds.upper;
      const ssef hl_bounds_scale = (hl_bounds_max-hl_bounds_min)*ssef(1.f/255.f);

      const ssef hl_bounds_scale_x = ssef(hl_bounds_scale[0]);
      const ssef hl_bounds_scale_y = ssef(hl_bounds_scale[1]);
      const ssef hl_bounds_scale_z = ssef(hl_bounds_scale[2]);
      
      const ssef hl_bounds_min_x = ssef(hl_bounds_min[0]);
      const ssef hl_bounds_min_y = ssef(hl_bounds_min[1]);
      const ssef hl_bounds_min_z = ssef(hl_bounds_min[2]);

      //      --stackPtr;
      NodeRef cur = NodeRef(0); //stackPtr->ptr);
      goto firstStepStartsHere;

      /* pop loop */
      while (true) {
      pop:

        /*! pop next node */
        if (unlikely(stackPtr == stack)) break;
        stackPtr--;
        cur = NodeRef(stackPtr->ptr);

        /*! if popped node is too far, pop next one */
        if (unlikely(*(float*)&stackPtr->dist > ray.tfar[k]))//ray.tfar[k]))
          continue;
        
        /* downtraversal loop */
        while (true) {
          /*! stop if we found a leaf */
          if (unlikely(cur >= 0)) break;
        firstStepStartsHere:

          const Hairlet::QuadNode *node = &hl->node[-cur];
          const ssei bitMask(255);

          // PRINT(hairletID);
           // PRINT((int*)(uint32&)node->lo_x[0]);
           // PRINT((int*)(uint32&)node->lo_y[0]);
           // PRINT((int*)(uint32&)node->lo_z[0]);
           // PRINT((int*)(uint32&)node->hi_x[0]);
           // PRINT((int*)(uint32&)node->hi_y[0]);
           // PRINT((int*)(uint32&)node->hi_z[0]);
          const ssei _node_lo_x = ssei((uint32&)node->lo_x[0]);
          const ssei _node_lo_y = ssei((uint32&)node->lo_y[0]);
          const ssei _node_lo_z = ssei((uint32&)node->lo_z[0]);
          const ssei _node_hi_x = ssei((uint32&)node->hi_x[0]);
          const ssei _node_hi_y = ssei((uint32&)node->hi_y[0]);
          const ssei _node_hi_z = ssei((uint32&)node->hi_z[0]);

#if OSPRAY_TARGET_AVX2
          const ssei shift(0,8,16,24);
          const ssei node_lo_x 
            = ssei(_mm_srav_epi32(_node_lo_x,shift)) & bitMask;
          const ssei node_lo_y 
            = ssei(_mm_srav_epi32(_node_lo_y,shift)) & bitMask;
          const ssei node_lo_z
            = ssei(_mm_srav_epi32(_node_lo_z,shift)) & bitMask;
          const ssei node_hi_x 
            = ssei(_mm_srav_epi32(_node_hi_x,shift)) & bitMask;
          const ssei node_hi_y 
            = ssei(_mm_srav_epi32(_node_hi_y,shift)) & bitMask;
          const ssei node_hi_z
            = ssei(_mm_srav_epi32(_node_hi_z,shift)) & bitMask;
          
#else
          const ssei shlScale((1<<24),(1<<16),(1<<8),1);
          const ssei node_lo_x
            = ssei(_mm_srli_epi32(_mm_mullo_epi32(_node_lo_x,shlScale),24));
          const ssei node_lo_y
            = ssei(_mm_srli_epi32(_mm_mullo_epi32(_node_lo_y,shlScale),24));
          const ssei node_lo_z
            = ssei(_mm_srli_epi32(_mm_mullo_epi32(_node_lo_z,shlScale),24));
          const ssei node_hi_x
            = ssei(_mm_srli_epi32(_mm_mullo_epi32(_node_hi_x,shlScale),24));
          const ssei node_hi_y
            = ssei(_mm_srli_epi32(_mm_mullo_epi32(_node_hi_y,shlScale),24));
          const ssei node_hi_z
            = ssei(_mm_srli_epi32(_mm_mullo_epi32(_node_hi_z,shlScale),24));
#endif
          const sseb box_valid = (node_lo_x <= node_hi_x);
          // const sseb box_valid
          //   = (node_lo_x <= node_hi_x)
          //   & (node_lo_y <= node_hi_y)
          //   & (node_lo_z <= node_hi_z)
          //   ;
          
          // currently we're adding the max radius to each box since
          // the boxes are built over the axis, not the tubes. may
          // change that soon.
          // const ssef maxRad(hl->maxRadius);

          // ideally, bake that into a ray transformation ...
          const ssef world_lo_x
            = madd(ssef(node_lo_x), hl_bounds_scale_x, hl_bounds_min_x);//-maxRad;
          const ssef world_lo_y
            = madd(ssef(node_lo_y), hl_bounds_scale_y, hl_bounds_min_y);//-maxRad;
          const ssef world_lo_z
            = madd(ssef(node_lo_z), hl_bounds_scale_z, hl_bounds_min_z);//-maxRad;
          const ssef world_hi_x
            = madd(ssef(node_hi_x), hl_bounds_scale_x, hl_bounds_min_x);//+maxRad;
          const ssef world_hi_y
            = madd(ssef(node_hi_y), hl_bounds_scale_y, hl_bounds_min_y);//+maxRad;
          const ssef world_hi_z
            = madd(ssef(node_hi_z), hl_bounds_scale_z, hl_bounds_min_z);//+maxRad;

          // PRINT(maxRad);
          const ssef t_lo_x = msub(world_lo_x,rdir.x,org_rdir.x);
          const ssef t_lo_y = msub(world_lo_y,rdir.y,org_rdir.y);
          const ssef t_lo_z = msub(world_lo_z,rdir.z,org_rdir.z);
          const ssef t_hi_x = msub(world_hi_x,rdir.x,org_rdir.x);
          const ssef t_hi_y = msub(world_hi_y,rdir.y,org_rdir.y);
          const ssef t_hi_z = msub(world_hi_z,rdir.z,org_rdir.z);
          
          const ssef t0_x = min(t_lo_x,t_hi_x);
          const ssef t0_y = min(t_lo_y,t_hi_y);
          const ssef t0_z = min(t_lo_z,t_hi_z);
          const ssef t1_x = max(t_lo_x,t_hi_x);
          const ssef t1_y = max(t_lo_y,t_hi_y);
          const ssef t1_z = max(t_lo_z,t_hi_z);
          
          const ssef t0 = max(t0_x,t0_y,t0_z,ray_t0);
          const ssef t1 = min(t1_x,t1_y,t1_z,ray_t1);
          const sseb vmask = (t0 <= t1) & box_valid;
          size_t mask = movemask(vmask);

          /*! if no child is hit, pop next node */
          if (unlikely(mask == 0))
            goto pop;
          
          /*! one child is hit, continue with that child */
          size_t r = __bscf(mask);
          if (likely(mask == 0)) {
            cur = (int16&)node->child[r];//node->child(r);
            // assert(cur != BVH4::emptyNode);
            continue;
          }
        
          /*! two children are hit, push far child, and continue with closer child */
          NodeRef c0 = (int16&)node->child[r]; //c0.prefetch(); 
          const unsigned int d0 = ((unsigned int*)&t0)[r];
          r = __bscf(mask);
          NodeRef c1 = (int16&)node->child[r]; //c1.prefetch(); 
          const unsigned int d1 = ((unsigned int*)&t0)[r];

          if (likely(mask == 0)) {
            // assert(stackPtr < stackEnd);
            if (d0 < d1) { 
              stackPtr->ptr = c1; stackPtr->dist = d1; stackPtr++; cur = c0; 
              continue; 
            } else { 
              stackPtr->ptr = c0; stackPtr->dist = d0; stackPtr++; cur = c1; 
              continue; 
            }
          }
          
          /*! Here starts the slow path for 3 or 4 hit children. We push
           *  all nodes onto the stack to sort them there. */
          stackPtr->ptr = c0; stackPtr->dist = d0; stackPtr++;
          stackPtr->ptr = c1; stackPtr->dist = d1; stackPtr++;
          
          /*! three children are hit, push all onto stack and sort 3
              stack items, continue with closest child */
          r = __bscf(mask);
          NodeRef c = (int16&)node->child[r]; unsigned int d = ((unsigned int*)&t0)[r]; 
          stackPtr->ptr = c; stackPtr->dist = d; stackPtr++;
          
          if (likely(mask == 0)) {
            sort(stackPtr[-1], stackPtr[-2], stackPtr[-3]);
            cur = (NodeRef)stackPtr[-1].ptr; stackPtr--;
            continue;
          }
          

          /*! four children are hit, push all onto stack and sort 4
              stack items, continue with closest child */
          r = __bscf(mask);
          c = (int16&)node->child[r]; d = ((unsigned int*)&t0)[r]; 
          stackPtr->ptr = c; stackPtr->dist = d; stackPtr++;
          sort(stackPtr[-1], stackPtr[-2], stackPtr[-3], stackPtr[-4]);
          cur = (NodeRef)stackPtr[-1].ptr; stackPtr--;
        }
        
        // /*! this is a leaf node */
#if REAL_INTERSECTION 
        while (1) {
          uint segID = hl->leaf[cur].hairID;// hl->segStart[;
          
          // mailboxing
          uint *mb = &mailbox[segID%32];
          if (*mb != segID) {
            const uint32 startVertex = segID; //hl->segStart[segID];
            *mb = segID;
            if (intersectSegment1(raySpace,org_k,hl,startVertex,k,ray)) {
              ray.primID[k] = (long(segID)*13)>>4;
              ray.geomID[k] = (long(segID)*13)>>4;

              // vec3fa N = vec3fa((ray.Ng.x[k],ray.Ng.y[k],ray.Ng.z[k]));
              vec3fa N 
                = ray.Ng.x[k] * raySpace.vx
                + ray.Ng.y[k] * raySpace.vy
                + ray.Ng.z[k] * raySpace.vz;
              ray.Ng.x[k] = N.x;
              ray.Ng.y[k] = N.y;
              ray.Ng.z[k] = N.z;

              ray_t1 = ray.tfar[k];
            }
          }
          if (hl->leaf[cur].eol) break;
          ++cur;
        }
#else
        const float hit_dist = *(float*)&stackPtr[-1].dist;
        if (hit_dist < ray.tfar[k]) {
          ray.tfar[k] =  hit_dist;

          int segID = hl->leaf[cur].hairID;
          // ray.primID[k] = (long(hl)*13)>>4;
          // ray.geomID[k] = (long(hl)*13)>>4;
          ray.primID[k] = (long(segID)*13)>>4;
          ray.geomID[k] = (long(segID)*13)>>4;
          ray_t1 = hit_dist;
        }
#endif
      }
// #endif
    }


    void intersectHairlet(const void* _valid,  /*!< pointer to valid mask */
                          void* ptr,          /*!< pointer to user data */
                          embree::Ray8 &ray,       /*!< ray packet to intersect */
                          size_t item         /*!< item to intersect */)
    {
      avxb valid = *(avxb*)_valid;

      Hairlet *hl = ((HairBVH*)ptr)->hairlet[item];
      avx3f ray_org = ray.org, ray_dir = ray.dir;
      // avxf ray_tnear = ray.tnear, ray.tfar  = ray.tfar;
      const avx3f rdir = rcp_safe(ray_dir);
      const avx3f org(ray_org), org_rdir = org * rdir;

      const embree::LinearSpace3<avx3f> raySpace = embree::frame(ray_dir);

      /* compute near/far per ray */
      // iw: not yet using this optimization ...
      avx3i nearXYZ;
      nearXYZ.x = select(rdir.x >= 0.0f,
                         avxi(0*(int)sizeof(ssef)),
                         avxi(1*(int)sizeof(ssef)));
      nearXYZ.y = select(rdir.y >= 0.0f,
                         avxi(2*(int)sizeof(ssef)),
                         avxi(3*(int)sizeof(ssef)));
      nearXYZ.z = select(rdir.z >= 0.0f,
                         avxi(4*(int)sizeof(ssef)),
                         avxi(5*(int)sizeof(ssef)));

      size_t bits = movemask(valid);
      if (bits==0) return;
      for (size_t i=__bsf(bits); bits!=0; bits=__btc(bits,i), i=__bsf(bits)) {
        hairIntersect1(item,hl,
                       i, ray, ray_org, ray_dir, rdir, //ray_tnear, ray_tfar, nearXYZ,
                       raySpace);
        // ray_tfar = ray.tfar;
      }
    }
  }
}
