
/* 
   A* -------------------------------------------------------------------
   B* This file contains source code for the PyMOL computer program
   C* copyright 1998-2000 by Warren Lyford Delano of DeLano Scientific. 
   D* -------------------------------------------------------------------
   E* It is unlawful to modify or remove this copyright notice.
   F* -------------------------------------------------------------------
   G* Please see the accompanying LICENSE file for further information. 
   H* -------------------------------------------------------------------
   I* Additional authors of this source file include:
   -* 
   -* 
   -*
   Z* -------------------------------------------------------------------
*/
#include"os_python.h"

#include"os_predef.h"
#include"os_std.h"
#include"os_gl.h"

#include"Base.h"
#include"OOMac.h"
#include"Vector.h"
#include"ObjectMolecule.h"
#include"RepCylBond.h"
#include"Color.h"
#include"Setting.h"
#include"main.h"
#include"Feedback.h"
#include"Sphere.h"
#include"ShaderMgr.h"
#include"Scene.h"

typedef struct RepCylBond {
  Rep R;
  //  float *V,
  float *VR; /* array (vertices/normals/etc. for normal rendering (V) and ray tracing (VR) */
  CGO *Vcgo, *VPcgo; /*regular rendering (Vcgo) and picking (VPcgo) CGO */
  int N, NR;
  int NEdge;
  float *VP; /* vertices for picking, aligned with Picking info in I->R.P */
  int NP;
  float *VSP, *VSPC;
  SphereRec *SP;
  int NSP, NSPC;
  float *VarAlpha, *VarAlphaRay, *VarAlphaSph;
  CGO *shaderCGO;
} RepCylBond;

void RepCylinder(PyMOLGlobals *G, RepCylBond *I, CGO *cgo, float *v1, float *v2, int nEdge,
		 int frontCap, int endCap, float tube_size, float overlap, float nub, float **dir, 
		 int shader_mode, float *v2color );

void RepCylBondFree(RepCylBond * I);

void RepCylBondFree(RepCylBond * I)
{
  if (I->Vcgo){
    CGOFree(I->Vcgo);
    I->Vcgo = 0;
  }
  if (I->VPcgo){
    CGOFree(I->VPcgo);
    I->VPcgo = 0;
  }
  FreeP(I->VarAlpha);
  FreeP(I->VarAlphaRay);
  FreeP(I->VarAlphaSph);
  FreeP(I->VR);
  VLAFreeP(I->VP);
  //  VLAFreeP(I->V);
  FreeP(I->VSP);
  FreeP(I->VSPC);
  RepPurge(&I->R);
  OOFreeP(I);
}

void RepCylinderBox(RepCylBond *I, CGO *cgo, float *v1, float *v2, float tube_size,
		    float overlap, float nub);

static void RepCylBondRender(RepCylBond * I, RenderInfo * info)
{
  CRay *ray = info->ray;
  Picking **pick = info->pick;
  int a;
  float *vptr, *var_alpha;
  int c, cc;
  float alpha;
  SphereRec *sp;
  register PyMOLGlobals *G = I->R.G;
  int width, height;

  SceneGetWidthHeight(G, &width, &height); 

  alpha =
    SettingGet_f(G, I->R.cs->Setting, I->R.obj->Setting, cSetting_stick_transparency);
  alpha = 1.0F - alpha;
  if(fabs(alpha - 1.0) < R_SMALL4)
    alpha = 1.0F;
  if(ray) {
    ray->fTransparentf(ray, 1.0F - alpha);
    PRINTFD(G, FB_RepCylBond)
      " RepCylBondRender: rendering raytracable...\n" ENDFD;

    vptr = I->VR;
    c = I->NR;
    var_alpha = I->VarAlphaRay;
    while(c--) {
      if(var_alpha) {
        ray->fTransparentf(ray, 1.0F - *(var_alpha++));
      }
      if (vptr[0] == vptr[3] && vptr[1] == vptr[4] && vptr[2] == vptr[5]){
	ray->fSausage3fv(ray, vptr + 7, vptr + 10, *(vptr + 6), vptr, vptr + 3);
      } else {
	float mid[3];
	average3f(vptr + 7, vptr + 10, mid);
	ray->fSausage3fv(ray, vptr + 7, mid, *(vptr + 6), vptr, vptr);
	ray->fSausage3fv(ray, mid, vptr + 10, *(vptr + 6), vptr + 3, vptr + 3);
      }
      vptr += 13;
    }
    var_alpha = I->VarAlphaSph;
    if(I->VSPC) {
      vptr = I->VSPC;
      c = I->NSPC;
      while(c--) {
        if(var_alpha) {
          ray->fTransparentf(ray, 1.0F - *(var_alpha++));
        }
        ray->fColor3fv(ray, vptr);
        vptr += 3;
        ray->fSphere3fv(ray, vptr, *(vptr + 3));
        vptr += 4;
      }
    }
    ray->fTransparentf(ray, 0.0);
  } else if(G->HaveGUI && G->ValidContext) {
    if(pick) {
      PRINTFD(G, FB_RepCylBond)
        " RepCylBondRender: rendering pickable...\n" ENDFD;

      if (I->VPcgo){
	CGORenderGLPicking(I->VPcgo, pick, &I->R.context, I->R.cs->Setting, I->R.obj->Setting);
      }
    } else { /* else not pick, i.e., when rendering */
      short use_shader, generate_shader_cgo = 0, use_display_lists = 0, shader_mode = 0;
      use_shader = (int) SettingGet(G, cSetting_stick_use_shader) & 
                           (int) SettingGet(G, cSetting_use_shaders);
      use_display_lists = (int) SettingGet(G, cSetting_use_display_lists);
      shader_mode = use_shader && (int) SettingGet(G, cSetting_stick_as_cylinders) 
	&& (int) SettingGet(G, cSetting_render_as_cylinders);

      if (I->shaderCGO && (!use_shader || CGOCheckWhetherToFree(G, I->shaderCGO))){
	CGOFree(I->shaderCGO);
	I->shaderCGO = 0;
      }

#ifdef _PYMOL_GL_CALLLISTS
        if(use_display_lists && I->R.displayList) {
          glCallList(I->R.displayList);
	  return;
	}
#endif

      if (use_shader){
	if (!I->shaderCGO){
	  I->shaderCGO = CGONew(G);
	  I->shaderCGO->use_shader = true;
	  generate_shader_cgo = 1;
	} else {
	  if (shader_mode == 1) { // GLSL
	    CShaderPrg *shaderPrg;
	    shaderPrg = CShaderPrg_Enable_CylinderShader(G);
	    {
	      float *color;
	      color = ColorGet(G, I->R.obj->Color);
	      I->shaderCGO->debug = SettingGet(G, cSetting_stick_debug);
	      I->shaderCGO->enable_shaders = 0;
	      CGORenderGL(I->shaderCGO, color, NULL, NULL, info, &I->R);
	    }
	    CShaderPrg_Disable(shaderPrg);
	    return;
	  } else {
	    CShaderPrg *shaderPrg;
	    shaderPrg = CShaderPrg_Enable_DefaultShader(G);
	    CShaderPrg_Set1i(shaderPrg, "lighting_enabled", !SettingGet(G, cSetting_stick_debug));
	    {
	      float *color;
	      color = ColorGet(G, I->R.obj->Color);
	      I->shaderCGO->debug = SettingGet(G, cSetting_stick_debug);
	      I->shaderCGO->enable_shaders = 0;
	      CGORenderGL(I->shaderCGO, color, NULL, NULL, info, &I->R);
	    }
	    CShaderPrg_Disable(shaderPrg);
	    return;
	  }
	}
      }
#ifdef _PYMOL_GL_CALLLISTS
      if(use_display_lists) {
	if(!I->R.displayList) {
	  I->R.displayList = glGenLists(1);
	  if(I->R.displayList) {
	    glNewList(I->R.displayList, GL_COMPILE_AND_EXECUTE);
	  }
	}
      }
#endif
      //      vptr = I->V;
      c = I->N;
      var_alpha = I->VarAlpha;
      PRINTFD(G, FB_RepCylBond)
	" RepCylBondRender: rendering GL...\n" ENDFD;
      
      if (generate_shader_cgo){
	if (I->Vcgo){
	  CGOAppend(I->shaderCGO, I->Vcgo);
	}
      } else {
	if (I->Vcgo){
	  float *color;
	  color = ColorGet(G, I->R.obj->Color);
	  I->Vcgo->debug = SettingGet(G, cSetting_stick_debug);
	  CGORenderGL(I->Vcgo, color, NULL, NULL, info, &I->R);
	}
      }
      if(I->VSP) {            /* stick_ball : stick spheres, if present */
	vptr = I->VSP;
	c = I->NSP;
	var_alpha = I->VarAlphaSph;
	  if (generate_shader_cgo){
	    if((alpha == 1.0) && !(var_alpha)) {
	      sp = I->SP;
	      while(c--) {
		CGOColorv(I->shaderCGO, vptr);
		vptr += 3;
		for(a = 0; a < sp->NStrip; a++) {
		  cc = sp->StripLen[a];
		  CGOBegin(I->shaderCGO, GL_TRIANGLE_STRIP);
		  cc = sp->StripLen[a];
		  while(cc--) {
		    CGONormalv(I->shaderCGO, vptr);
		    vptr += 3;
		    CGOVertexv(I->shaderCGO, vptr);
		    vptr += 3;
		  }
		  CGOEnd(I->shaderCGO);
		}
	      }
	    } else {
	      sp = I->SP;
	      while(c--) {
		if(!var_alpha) {
		  CGOAlpha(I->shaderCGO, alpha);
		} else {
		  CGOAlpha(I->shaderCGO, *(var_alpha++));
		}
		CGOColor(I->shaderCGO, vptr[0], vptr[1], vptr[2]);
		vptr += 3;
		for(a = 0; a < sp->NStrip; a++) {
		  cc = sp->StripLen[a];
		  CGOBegin(I->shaderCGO, GL_TRIANGLE_STRIP);
		  cc = sp->StripLen[a];
		  while(cc--) {
		    CGONormalv(I->shaderCGO, vptr);
		    vptr += 3;
		    CGOVertexv(I->shaderCGO, vptr);
		    vptr += 3;
		  }
		  CGOEnd(I->shaderCGO);
		}
	      }
	    }
	} else { /* end generate_shader_cgo */
	if((alpha == 1.0) && !(var_alpha)) {
	  sp = I->SP;
	  while(c--) {
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
	    glColor3fv(vptr);
#endif
	    vptr += 3;
	    for(a = 0; a < sp->NStrip; a++) {
	      cc = sp->StripLen[a];
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
#ifdef _PYMOL_GL_DRAWARRAYS
	      {
		int numverts = cc, pl;
		ALLOCATE_ARRAY(GLfloat,vertVals,numverts*3)
		ALLOCATE_ARRAY(GLfloat,normVals,numverts*3)
		pl = 0;
		while(cc--) {
		  normVals[pl] = vptr[0]; normVals[pl+1] = vptr[1]; normVals[pl+2] = vptr[2];
		  vptr += 3;
		  vertVals[pl++] = vptr[0]; vertVals[pl++] = vptr[1]; vertVals[pl++] = vptr[2];
		  vptr += 3;
		}
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_NORMAL_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, vertVals);
		glNormalPointer(GL_FLOAT, 0, normVals);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, numverts);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
		DEALLOCATE_ARRAY(vertVals)
		DEALLOCATE_ARRAY(normVals)
	      }
#else
	      glBegin(GL_TRIANGLE_STRIP);
	      cc = sp->StripLen[a];
	      while(cc--) {
		glNormal3fv(vptr);
		vptr += 3;
		glVertex3fv(vptr);
		vptr += 3;
	      }
	      glEnd();
#endif
#endif
	    }
	  }
	} else {
	  sp = I->SP;
	  while(c--) {
	    if(!var_alpha) {
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
	      glColor4f(vptr[0], vptr[1], vptr[2], alpha);
#endif
	    } else {
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
	      glColor4f(vptr[0], vptr[1], vptr[2], *(var_alpha++));
#endif
	    }
	    vptr += 3;
	    for(a = 0; a < sp->NStrip; a++) {
	      cc = sp->StripLen[a];
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
#ifdef _PYMOL_GL_DRAWARRAYS
	      {
		int numverts = cc, pl;
		ALLOCATE_ARRAY(GLfloat,vertVals,numverts*3)
		ALLOCATE_ARRAY(GLfloat,normVals,numverts*3)
		pl = 0;
		while(cc--) {
		  normVals[pl] = vptr[0]; normVals[pl+1] = vptr[1]; normVals[pl+2] = vptr[2];
		  vptr += 3;
		  vertVals[pl++] = vptr[0]; vertVals[pl++] = vptr[1]; vertVals[pl++] = vptr[2];
		  vptr += 3;
		}
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_NORMAL_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, vertVals);
		glNormalPointer(GL_FLOAT, 0, normVals);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, numverts);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
		DEALLOCATE_ARRAY(vertVals)
		DEALLOCATE_ARRAY(normVals)
	      }
#else
	      glBegin(GL_TRIANGLE_STRIP);
	      cc = sp->StripLen[a];
	      while(cc--) {
		glNormal3fv(vptr);
		vptr += 3;
		glVertex3fv(vptr);
		vptr += 3;
	      }
	      glEnd();
#endif
#endif
	    }
	  }
	}
	} /* end else use_shader */ 	
      }
      PRINTFD(G, FB_RepCylBond)
	" RepCylBondRender: done.\n" ENDFD;
      
      /* end of rendering, if using shaders, then render CGO */
      if (use_shader) {
	if (generate_shader_cgo){
	  CGO *convertcgo = NULL;
	  CGOStop(I->shaderCGO);

	  /*
#ifdef _PYMOL_CGO_DRAWARRAYS
	  convertcgo = CGOCombineBeginEnd(I->shaderCGO, 0);    
	  CGOFree(I->shaderCGO);    
	  I->shaderCGO = convertcgo;
#else
	  (void)convertcgo;
#endif
	  */
#ifdef _PYMOL_CGO_DRAWBUFFERS
	  if (shader_mode == 1) { //GLSL
	    convertcgo = CGOOptimizeGLSLCylindersToVBOIndexed(I->shaderCGO, 0);
	  } else {
	    convertcgo = CGOOptimizeToVBOIndexed(I->shaderCGO, 0);
	  }
	  if (convertcgo){
	    CGOFree(I->shaderCGO);
	    I->shaderCGO = convertcgo;
	  }
#else
	  (void)convertcgo;
#endif
	}

  if (shader_mode == 1) { // GLSL
    CShaderPrg *shaderPrg = CShaderPrg_Enable_CylinderShader(G);
    {
      float *color;
      color = ColorGet(G, I->R.obj->Color);
      I->shaderCGO->debug = SettingGet(G, cSetting_stick_debug);
      I->shaderCGO->enable_shaders = 0;
      CGORenderGL(I->shaderCGO, color, NULL, NULL, info, &I->R);
    }
    CShaderPrg_Disable(shaderPrg);
  } else {
    CShaderPrg *shaderPrg;
    shaderPrg = CShaderPrg_Enable_DefaultShader(G);
    CShaderPrg_Set1i(shaderPrg, "lighting_enabled", !SettingGet(G, cSetting_stick_debug));
    {
      float *color;
      color = ColorGet(G, I->R.obj->Color);
      I->shaderCGO->debug = SettingGet(G, cSetting_stick_debug);
      I->shaderCGO->enable_shaders = 0;
      CGORenderGL(I->shaderCGO, color, NULL, NULL, info, &I->R);
    }
    CShaderPrg_Disable(shaderPrg);
  }
  }
#ifdef _PYMOL_GL_CALLLISTS
      if (use_display_lists && I->R.displayList){
	glEndList();
	glCallList(I->R.displayList);      
      }
#endif
    }
  }
}

static void RepValence(RepCylBond *I, CGO *cgo, int *n_ptr,       /* opengl */
                       float **vr_ptr, int *nr_ptr,     /* ray */
                       float *v1, float *v2, int *other,
                       int a1, int a2, float *coord,
                       float *color1, float *color2, int ord,
                       int n_edge,
                       float tube_size,
                       float overlap,
                       float nub, int half_bonds, int fixed_r, float scale_r,
                       short shader_mode)
{

  float d[3], t[3], p0[3], p1[3], p2[3], *vv;
  float v1t[3], v2t[3], vh[3], *dir = 0;
  float *vr = *vr_ptr;
  int n = *n_ptr, nr = *nr_ptr;
  int a3;
  int double_sided;

  /* First, we need to construct a coordinate system */

  /* get direction vector */

  p0[0] = (v2[0] - v1[0]);
  p0[1] = (v2[1] - v1[1]);
  p0[2] = (v2[2] - v1[2]);

  copy3f(p0, d);
  normalize3f(p0);

  /* need a third atom to get planarity */
  a3 = ObjectMoleculeGetPrioritizedOther(other, a1, a2, &double_sided);

  if(a3 < 0) {
    t[0] = p0[0];
    t[1] = p0[1];
    t[2] = -p0[2];
  } else {
    vv = coord + 3 * a3;
    t[0] = *(vv++) - v1[0];
    t[1] = *(vv++) - v1[1];
    t[2] = *(vv++) - v1[2];
    normalize3f(t);
  }

  cross_product3f(d, t, p1);

  normalize3f(p1);

  if(length3f(p1) == 0.0) {
    p1[0] = p0[1];
    p1[1] = p0[2];
    p1[2] = p0[0];
    cross_product3f(p0, p1, p2);
    normalize3f(p2);
  } else {
    cross_product3f(d, p1, p2);

    normalize3f(p2);
  }

  /* we have a coordinate system */

  /* Next, we need to determine how many cylinders */

  switch (ord) {
  case 2:
    {
      float radius = tube_size;
      float overlap_r;
      float nub_r;
      if(!fixed_r) {
        radius *= scale_r;
        radius /= 2.5;
      }

      overlap_r = radius * overlap;
      nub_r = radius * nub;

      t[0] = p2[0] * 1.5F * radius;
      t[1] = p2[1] * 1.5F * radius;
      t[2] = p2[2] * 1.5F * radius;

      if(!half_bonds) {

        /* opengl */

	CGOColorv(cgo, color1);

        add3f(v1, t, v1t);
        add3f(v2, t, v2t);

	RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 1, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
        n++;

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius;
        copy3f(v1t, vr);
        vr += 3;
        copy3f(v2t, vr);
        vr += 3;
        nr++;

        /* opengl */

	CGOColorv(cgo, color1);
        subtract3f(v1, t, v1t);
        subtract3f(v2, t, v2t);

        RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 1, radius, overlap_r, nub_r, &dir, shader_mode, NULL);

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius;
        copy3f(v1t, vr);
        vr += 3;
        copy3f(v2t, vr);
        vr += 3;
        nr++;

        n++;
      } else {
        vh[0] = (v1[0] + v2[0]) * 0.5F;
        vh[1] = (v1[1] + v2[1]) * 0.5F;
        vh[2] = (v1[2] + v2[2]) * 0.5F;

        if(color1) {


	  CGOColorv(cgo, color1);
          /* opengl */
          add3f(v1, t, v1t);
          add3f(vh, t, v2t);

          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

	  CGOColorv(cgo, color1);
          /* opengl */
          subtract3f(v1, t, v1t);
          subtract3f(vh, t, v2t);

          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

        }
        if(color2) {

	  CGOColorv(cgo, color2);
          add3f(v2, t, v1t);
          add3f(vh, t, v2t);
          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color2, vr);
          vr += 3;
          copy3f(color2, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

	  CGOColorv(cgo, color2);

          subtract3f(v2, t, v1t);
          subtract3f(vh, t, v2t);

          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;
          /* ray */

          copy3f(color2, vr);
          vr += 3;
          copy3f(color2, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

        }
      }
    }
    break;
  case 3:
    {
      float radius = tube_size;
      float overlap_r;
      float nub_r;
      if(!fixed_r) {
        radius *= scale_r;
        radius /= 3.5;
      }

      overlap_r = radius * overlap;
      nub_r = radius * nub;

      t[0] = p2[0] * 2.5F * radius;
      t[1] = p2[1] * 2.5F * radius;
      t[2] = p2[2] * 2.5F * radius;

      if(!half_bonds) {
        /* opengl */
	CGOColorv(cgo, color1);

        copy3f(v1, v1t);
        copy3f(v2, v2t);
        RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 1, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
        n++;

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius;
        copy3f(v1, vr);
        vr += 3;
        copy3f(v2, vr);
        vr += 3;
        nr++;

        /* opengl */
	CGOColorv(cgo, color1);
        add3f(v1, t, v1t);
        add3f(v2, t, v2t);

	RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 1, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
        n++;

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius;
        copy3f(v1t, vr);
        vr += 3;
        copy3f(v2t, vr);
        vr += 3;
        nr++;

        /* opengl */
	CGOColorv(cgo, color1);
        subtract3f(v1, t, v1t);
        subtract3f(v2, t, v2t);

        RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 1, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
        n++;

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius;
        copy3f(v1t, vr);
        vr += 3;
        copy3f(v2t, vr);
        vr += 3;
        nr++;

      } else {
        vh[0] = (v1[0] + v2[0]) * 0.5F;
        vh[1] = (v1[1] + v2[1]) * 0.5F;
        vh[2] = (v1[2] + v2[2]) * 0.5F;

        if(color1) {
          /* opengl */
	  CGOColorv(cgo, color1);

          copy3f(v1, v1t);
          RepCylinder(NULL, I, cgo, v1t, vh, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1, vr);
          vr += 3;
          copy3f(vh, vr);
          vr += 3;
          nr++;

          /* opengl */
	  CGOColorv(cgo, color1);
          add3f(v1, t, v1t);
          add3f(vh, t, v2t);

          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

          /* opengl */
	  CGOColorv(cgo, color1);

          subtract3f(v1, t, v1t);
          subtract3f(vh, t, v2t);

          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

        }
        if(color2) {

          /* opengl */
	  CGOColorv(cgo, color2);

          copy3f(v2, v2t);
          RepCylinder(NULL, I, cgo, v2t, vh, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color2, vr);
          vr += 3;
          copy3f(color2, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v2, vr);
          vr += 3;
          copy3f(vh, vr);
          vr += 3;
          nr++;

          /* opengl */
	  CGOColorv(cgo, color2);

          add3f(v2, t, v1t);
          add3f(vh, t, v2t);

          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color2, vr);
          vr += 3;
          copy3f(color2, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

          /* opengl */
	  CGOColorv(cgo, color2);

          subtract3f(v2, t, v1t);
          subtract3f(vh, t, v2t);

          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color2, vr);
          vr += 3;
          copy3f(color2, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1t, vr);
          vr += 3;
          copy3f(v2t, vr);
          vr += 3;
          nr++;

        }
      }
    }

    break;
  case 4:
    {
      float radius = tube_size;
      float radius2 = tube_size;
      float overlap_r, overlap_r2;
      float nub_r, nub_r2;
      float along[3], adj[3], v1tt[3], v2tt[3];
      float inner1a = 0.24F;
      float inner1b = 0.44F;
      float inner2a = 0.5F + (0.5F - inner1b);
      float inner2b = 1.0F - inner1a;

      if(!fixed_r) {
        radius *= scale_r;
        radius2 = radius / 2.5F;
        t[0] = p2[0] * 1.5F * radius;
        t[1] = p2[1] * 1.5F * radius;
        t[2] = p2[2] * 1.5F * radius;
      } else {
        inner1a -= 0.04F;
        inner2b = 1.0F - inner1a;
        t[0] = p2[0] * 3 * radius;
        t[1] = p2[1] * 3 * radius;
        t[2] = p2[2] * 3 * radius;
      }

      overlap_r = radius * overlap;
      nub_r = radius * nub;
      overlap_r2 = radius2 * overlap;
      nub_r2 = radius2 * nub;

      if(!half_bonds) {

        /* opengl */

	CGOColorv(cgo, color1);

        copy3f(v1, v1t);
        copy3f(v2, v2t);
        RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 1, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
        n++;

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius;
        copy3f(v1, vr);
        vr += 3;
        copy3f(v2, vr);
        vr += 3;
        nr++;

        subtract3f(v1, t, v1t);
        subtract3f(v2, t, v2t);

        subtract3f(v2t, v1t, along);
        scale3f(along, inner1a, adj);
        add3f(adj, v1t, v1tt);
        scale3f(along, inner1b, adj);
        add3f(adj, v1t, v2tt);

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius2;
        copy3f(v1tt, vr);
        vr += 3;
        copy3f(v2tt, vr);
        vr += 3;
        nr++;

        /* opengl */

	CGOColorv(cgo, color1);

        RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
        n++;

        scale3f(along, inner2a, adj);
        add3f(adj, v1t, v1tt);
        scale3f(along, inner2b, adj);
        add3f(adj, v1t, v2tt);

        /* ray */

        copy3f(color1, vr);
        vr += 3;
        copy3f(color1, vr);
        vr += 3;
        *(vr++) = radius2;
        copy3f(v1tt, vr);
        vr += 3;
        copy3f(v2tt, vr);
        vr += 3;
        nr++;

        /* opengl */

	CGOColorv(cgo, color1);

        RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
        n++;

        if(double_sided) {

          add3f(v1, t, v1t);
          add3f(v2, t, v2t);

          subtract3f(v2t, v1t, along);
          scale3f(along, inner1a, adj);
          add3f(adj, v1t, v1tt);
          scale3f(along, inner1b, adj);
          add3f(adj, v1t, v2tt);

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius2;
          copy3f(v1tt, vr);
          vr += 3;
          copy3f(v2tt, vr);
          vr += 3;
          nr++;

          /* opengl */

	  CGOColorv(cgo, color1);

          RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
          n++;

          scale3f(along, inner2a, adj);
          add3f(adj, v1t, v1tt);
          scale3f(along, inner2b, adj);
          add3f(adj, v1t, v2tt);

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius2;
          copy3f(v1tt, vr);
          vr += 3;
          copy3f(v2tt, vr);
          vr += 3;
          nr++;

          /* opengl */

	  CGOColorv(cgo, color1);

          RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
          n++;

        }

      } else {
        vh[0] = (v1[0] + v2[0]) * 0.5F;
        vh[1] = (v1[1] + v2[1]) * 0.5F;
        vh[2] = (v1[2] + v2[2]) * 0.5F;

        if(color1) {

          /* opengl */

	  CGOColorv(cgo, color1);

          copy3f(v1, v1t);
          copy3f(vh, v2t);
          RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v1, vr);
          vr += 3;
          copy3f(vh, vr);
          vr += 3;
          nr++;

          subtract3f(v1, t, v1t);
          subtract3f(v2, t, v2t);

          subtract3f(v2t, v1t, along);
          scale3f(along, inner1a, adj);
          add3f(adj, v1t, v1tt);
          scale3f(along, inner1b, adj);
          add3f(adj, v1t, v2tt);

          /* ray */

          copy3f(color1, vr);
          vr += 3;
          copy3f(color1, vr);
          vr += 3;
          *(vr++) = radius2;
          copy3f(v1tt, vr);
          vr += 3;
          copy3f(v2tt, vr);
          vr += 3;
          nr++;

          /* open gl */

	  CGOColorv(cgo, color1);

          RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
          n++;

          if(double_sided) {

            add3f(v1, t, v1t);
            add3f(v2, t, v2t);

            subtract3f(v2t, v1t, along);
            scale3f(along, inner1a, adj);
            add3f(adj, v1t, v1tt);
            scale3f(along, inner1b, adj);
            add3f(adj, v1t, v2tt);

            /* ray */

            copy3f(color1, vr);
            vr += 3;
            copy3f(color1, vr);
            vr += 3;
            *(vr++) = radius2;
            copy3f(v1tt, vr);
            vr += 3;
            copy3f(v2tt, vr);
            vr += 3;
            nr++;

            /* open gl */

	    CGOColorv(cgo, color1);

            RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
            n++;

          }
        }

        if(color2) {

	  CGOColorv(cgo, color2);

          copy3f(v2, v1t);
          copy3f(vh, v2t);

	  RepCylinder(NULL, I, cgo, v1t, v2t, n_edge, 1, 0, radius, overlap_r, nub_r, &dir, shader_mode, NULL);
          n++;

          /* ray */

          copy3f(color2, vr);
          vr += 3;
          copy3f(color2, vr);
          vr += 3;
          *(vr++) = radius;
          copy3f(v2, vr);
          vr += 3;
          copy3f(vh, vr);
          vr += 3;
          nr++;

          subtract3f(v1, t, v1t);
          subtract3f(v2, t, v2t);

          subtract3f(v2t, v1t, along);
          scale3f(along, inner2a, adj);
          add3f(adj, v1t, v1tt);
          scale3f(along, inner2b, adj);
          add3f(adj, v1t, v2tt);

          /* ray */

          copy3f(color2, vr);
          vr += 3;
          copy3f(color2, vr);
          vr += 3;
          *(vr++) = radius2;
          copy3f(v1tt, vr);
          vr += 3;
          copy3f(v2tt, vr);
          vr += 3;
          nr++;

          /* opengl */

	  CGOColorv(cgo, color2);

          RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
          n++;

          if(double_sided) {

            add3f(v1, t, v1t);
            add3f(v2, t, v2t);

            subtract3f(v2t, v1t, along);
            scale3f(along, inner2a, adj);
            add3f(adj, v1t, v1tt);
            scale3f(along, inner2b, adj);
            add3f(adj, v1t, v2tt);

            /* ray */

            copy3f(color2, vr);
            vr += 3;
            copy3f(color2, vr);
            vr += 3;
            *(vr++) = radius2;
            copy3f(v1tt, vr);
            vr += 3;
            copy3f(v2tt, vr);
            vr += 3;
            nr++;

            /* opengl */

	    CGOColorv(cgo, color2);
            RepCylinder(NULL, I, cgo, v1tt, v2tt, n_edge, 1, 1, radius2, overlap_r2, nub_r2, &dir, shader_mode, NULL);
            n++;

          }
        }
      }
    }
    break;
  }
  *n_ptr = n;
  *vr_ptr = vr;
  *nr_ptr = nr;
}

void RepCylBondFilterBond(int *marked, AtomInfoType *ati1, AtomInfoType *ati2, int b1, int b2, int na_mode, int *c1, int *c2, int *s1, int *s2){
  register char *name1 = ati1->name;
  register int prot1 = ati1->protons;
  register char *name2 = ati2->name;
  register int prot2 = ati2->protons;
  if(prot1 == cAN_C) {
    if((name1[1] == 'A') && (name1[0] == 'C') && (!name1[2])) { /* CA */
      if(prot2 == cAN_C) {
	if((name2[1] == 'B') && (name2[0] == 'C') && (!name2[2]))
	  *c1 = *c2;      /* CA-CB */
	else if((!name2[1]) && (name2[0] == 'C') && (!marked[b2]))
	  *s1 = *s2 = 0;  /* suppress CA-C */
      } else if(prot2 == cAN_H)
	*s1 = *s2 = 0;    /* suppress all CA-hydrogens */
    } else if((na_mode == 1) && (prot2 == cAN_C)) {
      if((((name2[3] == 0) &&
	   ((name2[2] == '*') || (name2[2] == '\'')) &&
	   (name2[1] == '5') &&
	   (name2[0] == 'C'))) &&
	 (((name1[3] == 0) &&
	   ((name1[2] == '*') || (name1[2] == '\'')) &&
	   (name1[1] == '4') && (name1[0] == 'C'))))
	*s1 = *s2 = 0;
    }
  } else if(prot1 == cAN_N) {
    if((!name1[1]) && (name1[0] == 'N')) {      /* N */
      if(prot2 == cAN_C) {
	if((name2[1] == 'D') && (name2[0] == 'C') && (!name2[2]))
	  *c1 = *c2;      /* N->CD in PRO */
	else if((name2[1] == 'A') && (name2[0] == 'C') && (!name2[2])
		&& (!marked[b1])) {
	  char *resn2 = ati2->resn;
	  if(!((resn2[0] == 'P') && (resn2[1] == 'R') && (resn2[2] == 'O')))
	    *s1 = *s2 = 0;        /* suppress N-CA, except in pro */
	  else
	    *c1 = *c2;
	} else if((!name2[1]) && (name2[0] == 'C') && (!marked[b1]))
	  *s1 = *s2 = 0;  /* suppress N-C */
      } else if(prot2 == cAN_H)
	*s1 = *s2 = 0;    /* suppress all N-hydrogens */
    }
  } else if((prot1 == cAN_O) && (prot2 == cAN_C)) {
    if((!name2[1]) && (name2[0] == 'C') &&
       (((!name1[1]) && (name1[0] == 'O')) ||
	((name1[3] == 0) && (name1[2] == 'T') && (name1[1] == 'X')
	 && (name1[0] == 'O')))
       && (!marked[b2]))
      *s1 = *s2 = 0;      /* suppress C-O,OXT */
    else if(na_mode == 1) {
      if((((name2[3] == 0) &&
	   ((name2[2] == '*') || (name2[2] == '\'')) &&
	   ((name2[1] == '3') || (name2[1] == '5')) &&
	   (name2[0] == 'C'))) &&
	 (((name1[3] == 0) &&
	   ((name1[2] == '*') || (name1[2] == '\'')) &&
	   ((name1[1] == '3') || (name1[1] == '5')) && (name1[0] == 'O'))))
	*s1 = *s2 = 0;
    }
  } else if((prot1 == cAN_P) && (prot2 == cAN_O)) {
    if((!name1[1]) && (name1[0] == 'P') &&
       (((name2[3] == 0) && (name2[2] == 'P') &&
	 ((name2[1] == '1') || (name2[1] == '2') || (name2[1] == '3'))
	 && (name2[0] == 'O'))))
      *s1 = *s2 = 0;      /* suppress P-O1P,O2P,O3P */
    else if(na_mode == 1) {
      if((!name1[1]) && (name1[0] == 'P') &&
	 (((name2[3] == 0) &&
	   ((name2[2] == '*') || (name2[2] == '\'')) &&
	   ((name2[1] == '3') || (name2[1] == '5')) && (name2[0] == 'O'))))
	*s1 = *s2 = 0;
    }
  }
  if(prot2 == cAN_C) {
    if((name2[1] == 'A') && (name2[0] == 'C') && (!name2[2])) { /* CA */
      if(prot1 == cAN_C) {
	if((name1[1] == 'B') && (name1[0] == 'C') && (!name1[2]))
	  *c2 = *c1;      /* CA-CB */
	else if((!name1[1]) && (name1[0] == 'C') && (!marked[b1]))
	  *s1 = *s2 = 0;  /* suppress CA-C */
      } else if(prot1 == cAN_H)
	*s1 = *s2 = 0;    /* suppress all CA-hydrogens */
    } else if((na_mode == 1) && (prot2 == cAN_C)) {
      if((((name1[3] == 0) &&
	   ((name1[2] == '*') || (name1[2] == '\'')) &&
	   (name1[1] == '5') &&
	   (name1[0] == 'C'))) &&
	 (((name2[3] == 0) &&
	   ((name2[2] == '*') || (name2[2] == '\'')) &&
	   (name2[1] == '4') && (name2[0] == 'C'))))
	*s1 = *s2 = 0;
    }
  } else if(prot2 == cAN_N) {
    if((!name2[1]) && (name2[0] == 'N')) {      /* N */
      if(prot1 == cAN_C) {
	if((name1[1] == 'D') && (name1[0] == 'C') && (!name1[2]))
	  *c2 = *c1;      /* N->CD in PRO */
	else if((name1[1] == 'A') && (name1[0] == 'C') && (marked[b2])) {
	  char *resn1 = ati1->resn;
	  if(!((resn1[0] == 'P') && (resn1[1] == 'R') && (resn1[2] == 'O')))
	    *s1 = *s2 = 0;        /* suppress N-CA, except in pro */
	  else
	    *c2 = *c1;
	} else if((!name1[1]) && (name1[0] == 'C') && (!marked[b2]))
	  *s1 = *s2 = 0;  /* suppress N-C */
      } else if(prot1 == cAN_H)
	*s1 = *s2 = 0;    /* suppress all N-hydrogens */
    }
  } else if((prot2 == cAN_O) && (prot1 == cAN_C)) {
    if((!name1[1]) && (name1[0] == 'C') &&
       (((!name2[1]) && (name2[0] == 'O')) ||
	((name2[3] == 0) && (name2[2] == 'T') && (name2[1] == 'X')
	 && (name2[0] == 'O')))
       && (!marked[b1]))
      *s1 = *s2 = 0;      /* suppress C-O,OXT */
    else if(na_mode == 1) {
      if((((name1[3] == 0) &&
	   ((name1[2] == '*') || (name1[2] == '\'')) &&
	   ((name1[1] == '3') || (name1[1] == '5')) &&
	   (name1[0] == 'C'))) &&
	 (((name2[3] == 0) &&
	   ((name2[2] == '*') || (name2[2] == '\'')) &&
	   ((name2[1] == '3') || (name2[1] == '5')) && (name2[0] == 'O'))))
	*s1 = *s2 = 0;
    }
  } else if((prot2 == cAN_P) && (prot1 == cAN_O)) {
    if((!name2[1]) && (name2[0] == 'P') &&
       (((name1[3] == 0) && (name1[2] == 'P') &&
	 ((name1[1] == '1') || (name1[1] == '2') || (name1[1] == '3'))
	 && (name1[0] == 'O'))))
      *s1 = *s2 = 0;      /* suppress P-O1P,O2P,O3P */
    else if(na_mode == 1) {
      if((!name2[1]) && (name2[0] == 'P') &&
	 (((name1[3] == 0) &&
	   ((name1[2] == '*') || (name1[2] == '\'')) &&
	   ((name1[1] == '3') || (name1[1] == '5')) && (name1[0] == 'O'))))
	*s1 = *s2 = 0;
    }
  }
}
void RepCylBondPopulateAdjacentAtoms(int **adjacent_atoms, ObjectMolecule *obj, CoordSet * cs, int *marked){
  PyMOLGlobals *G = cs->State.G;
  register BondType *b = obj->Bond;
  int a, ord, a1, a2, stick_color, c1, c2, s1, s2, half_bonds, hide_long = false;
  register int b1, b2;
  float *vv1, *vv2;
  float radius, transp, h_scale;
  int cartoon_side_chain_helper = 0;
  int ribbon_side_chain_helper = 1;
  int na_mode;
  const float _0p9 = 0.9F;
  int n_bonds = 0, n_bonds_bc = 0;

  radius = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_radius);
  stick_color = SettingGet_color(G, cs->Setting, obj->Obj.Setting, cSetting_stick_color);
  transp = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_transparency);
  h_scale = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_h_scale);
  half_bonds = (int) SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_half_bonds);
  hide_long = SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_hide_long_bonds);
  cartoon_side_chain_helper = SettingGet_b(G, cs->Setting, obj->Obj.Setting,
                                           cSetting_cartoon_side_chain_helper);
  ribbon_side_chain_helper = SettingGet_b(G, cs->Setting, obj->Obj.Setting,
                                          cSetting_ribbon_side_chain_helper);
  na_mode =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_nucleic_acid_mode);

  for(a = 0; a < obj->NBond; a++) {
    b1 = b->index[0];
    b2 = b->index[1];
    ord = b->order;
    if(obj->DiscreteFlag) {
      if((cs == obj->DiscreteCSet[b1]) && (cs == obj->DiscreteCSet[b2])) {
	a1 = obj->DiscreteAtmToIdx[b1];
	a2 = obj->DiscreteAtmToIdx[b2];
      } else {
	a1 = -1;
	a2 = -1;
      }
    } else {
      a1 = cs->AtmToIdx[b1];
      a2 = cs->AtmToIdx[b2];
    }
    if((a1 >= 0) && (a2 >= 0)) {
      register AtomInfoType *ati1 = obj->AtomInfo + b1;
      register AtomInfoType *ati2 = obj->AtomInfo + b2;
      int bd_stick_color;
      AtomInfoGetBondSetting_color(G, b, cSetting_stick_color, stick_color,
				   &bd_stick_color);
      if(bd_stick_color < 0) {
	if(bd_stick_color == cColorObject) {
	  c1 = (c2 = obj->Obj.Color);
	} else if(ColorCheckRamped(G, bd_stick_color)) {
	  c1 = (c2 = bd_stick_color);
	} else {
	  c1 = *(cs->Color + a1);
	  c2 = *(cs->Color + a2);
	}
      } else {
	c1 = (c2 = bd_stick_color);
      }
      vv1 = cs->Coord + 3 * a1;
      vv2 = cs->Coord + 3 * a2;
      
      s1 = ati1->visRep[cRepCyl];
      s2 = ati2->visRep[cRepCyl];
      
      if(!(s1 && s2))
	if(!half_bonds) {
	  s1 = 0;
	  s2 = 0;
	}
      
      if (s1||s2){
	n_bonds_bc++;
      }
      if(hide_long && (s1 || s2)) {
	float cutoff = (ati1->vdw + ati2->vdw) * _0p9;
	//	ai1 = obj->AtomInfo + b1;
	//	ai2 = obj->AtomInfo + b2;
	if(!within3f(vv1, vv2, cutoff))       /* atoms separated by more than 90% of the sum of their vdw radii */
	  s1 = s2 = 0;
      }
      
      if((!ati1->hetatm) && (!ati2->hetatm) &&
	 ((cartoon_side_chain_helper && ati1->visRep[cRepCartoon]
	   && ati2->visRep[cRepCartoon]) || (ribbon_side_chain_helper
					     && ati1->visRep[cRepRibbon]
					     && ati2->visRep[cRepRibbon]))) {
	RepCylBondFilterBond(marked, ati1, ati2, b1, b2, na_mode, &c1, &c2, &s1, &s2);
      }
      if((s1 || s2)) {
	/* This is a bond that is rendered as a stick */
	n_bonds++;
	if (!adjacent_atoms[a1]){
	  adjacent_atoms[a1] = Calloc(int, 2);
	  adjacent_atoms[a1][0] = 1; adjacent_atoms[a1][1] = a2;
	} else {
	  int len = adjacent_atoms[a1][0], *ptr = adjacent_atoms[a1], cnt;
	  adjacent_atoms[a1] = Calloc(int, len+2);
	  adjacent_atoms[a1][0] = len+1; 
	  for (cnt = 1; cnt<=len; cnt++){
	    adjacent_atoms[a1][cnt] = ptr[cnt];
	  }
	  adjacent_atoms[a1][len+1] = a2;
	  FreeP(ptr);
	}

	if (!adjacent_atoms[a2]){
	  adjacent_atoms[a2] = Calloc(int, 2);
	  adjacent_atoms[a2][0] = 1; adjacent_atoms[a2][1] = a1;
	} else {
	  int len = adjacent_atoms[a2][0], *ptr = adjacent_atoms[a2], cnt;
	  adjacent_atoms[a2] = Calloc(int, len+2);
	  adjacent_atoms[a2][0] = len+1; 
	  for (cnt = 1; cnt<=len; cnt++){
	    adjacent_atoms[a2][cnt] = ptr[cnt];
	  }
	  adjacent_atoms[a2][len+1] = a1;	  
	  FreeP(ptr);
	}
      }
    }
    b++;
  }
}



Rep *RepCylBondNew(CoordSet * cs, int state)
{
  PyMOLGlobals *G = cs->State.G;
  ObjectMolecule *obj;
  int a, a1, a2, c1, c2, s1, s2;
  register int b1, b2;
  register BondType *b;
  float *vv1, *vv2, *v0, *vr, *vsp, *vspc;
  float v1[3], v2[3], h[3];
  float radius;
  int nEdge;
  float valence;
  float overlap, nub;
  int stick_round_nub;
  int half_bonds, *other = NULL;
  int visFlag;
  int maxCyl;
  int ord;
  int stick_ball, stick_ball_color = -1;
  float stick_ball_ratio = 1.0F;
  unsigned int v_size, vr_size;
  register AtomInfoType *ai1, *ai2;
  SphereRec *sp = NULL;
  float *rgb1, *rgb2, rgb1_buf[3], rgb2_buf[3];
  int fixed_radius = false;
  int caps_req = true;
  int valence_flag = false;
  int hide_long = false;
  int stick_color = 0;
  int cartoon_side_chain_helper = 0;
  int ribbon_side_chain_helper = 1;
  int na_mode;
  int *marked = NULL, **adjacent_atoms = NULL;
  short *capdrawn = NULL;
  float scale_r = 1.0F;
  int variable_alpha = false;
  int n_var_alpha = 0, n_var_alpha_ray = 0, n_var_alpha_sph = 0;
  float transp, h_scale;
  int valence_found = false;
  const float _0p9 = 0.9F;
  float alpha;
  int n_bonds = 0;
  short shader_mode = 0;
  CGO *Vcgo = 0;
  OOAlloc(G, RepCylBond);

  I->Vcgo = 0;
  I->VPcgo = 0;
  PRINTFD(G, FB_RepCylBond)
    " RepCylBondNew-Debug: entered.\n" ENDFD;
  obj = cs->Obj;
  visFlag = false;
  b = obj->Bond;
  ai1 = obj->AtomInfo;
  if(obj->RepVisCache[cRepCyl])
    for(a = 0; a < obj->NBond; a++) {
      b1 = b->index[0];
      b2 = b->index[1];

      b++;
      if(ai1[b1].visRep[cRepCyl] || ai1[b2].visRep[cRepCyl]) {
        visFlag = true;
        break;
      }
    }
  if(!visFlag) {
    OOFreeP(I);
    return (NULL);              /* skip if no dots are visible */
  }

  marked = Calloc(int, obj->NAtom);
  capdrawn = Calloc(short, obj->NAtom);
  if (SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_good_geometry)){
    adjacent_atoms = Calloc(int*, obj->NAtom);
  }

  valence = SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_valence);
  valence_flag = (valence != 0.0F);
  maxCyl = 0;

  alpha =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_transparency);
  alpha = 1.0F - alpha;
  if(fabs(alpha - 1.0) < R_SMALL4)
    alpha = 1.0F;

  stick_color = SettingGet_color(G, cs->Setting, obj->Obj.Setting, cSetting_stick_color);
  cartoon_side_chain_helper = SettingGet_b(G, cs->Setting, obj->Obj.Setting,
                                           cSetting_cartoon_side_chain_helper);
  ribbon_side_chain_helper = SettingGet_b(G, cs->Setting, obj->Obj.Setting,
                                          cSetting_ribbon_side_chain_helper);

  transp = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_transparency);
  hide_long = SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_hide_long_bonds);

  b = obj->Bond;
  for(a = 0; a < obj->NBond; a++) {
    b1 = b->index[0];
    b2 = b->index[1];
    ord = b->order;

    if(obj->DiscreteFlag) {
      if((cs == obj->DiscreteCSet[b1]) && (cs == obj->DiscreteCSet[b2])) {
        a1 = obj->DiscreteAtmToIdx[b1];
        a2 = obj->DiscreteAtmToIdx[b2];
      } else {
        a1 = -1;
        a2 = -1;
      }
    } else {
      a1 = cs->AtmToIdx[b1];
      a2 = cs->AtmToIdx[b2];
    }
    if((a1 >= 0) && (a2 >= 0)) {
      int bd_valence_flag;
      if((!variable_alpha) && AtomInfoCheckBondSetting(G, b, cSetting_stick_transparency))
        variable_alpha = true;
      AtomInfoGetBondSetting_b(G, b, cSetting_valence, valence_flag, &bd_valence_flag);

      if(bd_valence_flag) {
        valence_found = true;
        switch (ord) {
        case 1:
          maxCyl += 2;
          break;
        case 2:
          maxCyl += 4;
          break;
        case 3:
          maxCyl += 6;
          break;
        case 4:
          maxCyl += 6;
          break;
        }
      } else
        maxCyl += 2;
    }
    b++;
  }

  nEdge = (int) SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_quality);
  radius = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_radius);
  half_bonds = (int) SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_half_bonds);
  na_mode =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_nucleic_acid_mode);
  h_scale = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_h_scale);
  RepInit(G, &I->R);
  I->R.fRender = (void (*)(struct Rep *, RenderInfo *)) RepCylBondRender;
  I->R.fFree = (void (*)(struct Rep *)) RepCylBondFree;
  I->R.obj = (CObject *) obj;
  I->R.cs = cs;
  I->R.context.object = (void *) obj;
  I->R.context.state = state;

  I->VR = NULL;
  I->N = 0;
  I->NR = 0;
  I->VP = NULL;
  I->SP = NULL;
  I->VSP = NULL;
  I->NSP = 0;
  I->VSPC = NULL;
  I->NSPC = 0;
  I->VarAlpha = NULL;
  I->VarAlphaRay = NULL;
  I->VarAlphaSph = NULL;
  I->shaderCGO = 0;

  if(obj->NBond) {
    int draw_mode = SettingGetGlobal_i(G, cSetting_draw_mode);
    int draw_quality = (((draw_mode == 1) || (draw_mode == -2) || (draw_mode == 2)));
    stick_ball = SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_stick_ball);
    overlap = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_overlap);
    nub = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_nub);
    stick_round_nub = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_round_nub);

    if(draw_quality) {
      nEdge = 16;
      overlap = 0.05;
    }

    shader_mode = (int)SettingGet(G, cSetting_use_shaders) && (int) SettingGet(G, cSetting_stick_as_cylinders) 
      && (int) SettingGet(G, cSetting_render_as_cylinders) && (int)SettingGet(G, cSetting_stick_use_shader);

    if(cartoon_side_chain_helper || ribbon_side_chain_helper) {
      /* mark atoms that are bonded to atoms without a
         visible cartoon or ribbon */
      b = obj->Bond;
      for(a = 0; a < obj->NBond; a++) {
        b1 = b->index[0];
        b2 = b->index[1];
        ord = b->order;
        if(obj->DiscreteFlag) {
          if((cs == obj->DiscreteCSet[b1]) && (cs == obj->DiscreteCSet[b2])) {
            a1 = obj->DiscreteAtmToIdx[b1];
            a2 = obj->DiscreteAtmToIdx[b2];
          } else {
            a1 = -1;
            a2 = -1;
          }
        } else {
          a1 = cs->AtmToIdx[b1];
          a2 = cs->AtmToIdx[b2];
        }
        if((a1 >= 0) && (a2 >= 0)) {
          register AtomInfoType *ati1 = obj->AtomInfo + b1;
          register AtomInfoType *ati2 = obj->AtomInfo + b2;

          if((!ati1->hetatm) && (!ati2->hetatm)) {
            if(((cartoon_side_chain_helper && ati1->visRep[cRepCartoon]
                 && !ati2->visRep[cRepCartoon]) || (ribbon_side_chain_helper
                                                    && ati1->visRep[cRepRibbon]
                                                    && !ati2->visRep[cRepRibbon]))) {
              marked[b1] = 1;
            }
            if(((cartoon_side_chain_helper && ati2->visRep[cRepCartoon]
                 && !ati1->visRep[cRepCartoon]) || (ribbon_side_chain_helper
                                                    && ati2->visRep[cRepRibbon]
                                                    && !ati1->visRep[cRepRibbon]))) {
              marked[b2] = 1;
            }
          }
        }
        b++;
      }

    }

    if(valence_found) {         /* build list of up to 2 connected atoms for each atom */
      other = ObjectMoleculeGetPrioritizedOtherIndexList(obj, cs);
      fixed_radius =
        SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_stick_fixed_radius);
      scale_r =
        SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_valence_scale);
    }

    /* OpenGL */

    v_size = maxCyl * ((9 + 6 + 6) * (nEdge + 1) + 3) * nEdge;
    //    v_size = maxCyl * ((9 + 6 + 6) * (nEdge + 1) + 3) ;
    /* each cylinder is 9+6+6+3= 21*(nEdge+1) floats for coordinates
       plus 3 more floats for color */

    if ( shader_mode == 1) { // GLSL
      v_size = maxCyl * (3 + 3 + 3 + 3);
      // In GLSL representation each cylinder uses 3 floats for origin,
      // 3 floats for axis, 3 floats for corner and 3 floats
      // for color = 12 floats total
    }

    if(variable_alpha)
      I->VarAlpha = Alloc(float, maxCyl);
    /*    I->V = VLAlloc(float, v_size);
	  ErrChkPtr(G, I->V);*/

    Vcgo = CGONew(G);

    /* RayTrace */

    vr_size = maxCyl * 11 * 3;
    if(variable_alpha)
      I->VarAlphaRay = Alloc(float, maxCyl);
    I->VR = Alloc(float, vr_size);
    ErrChkPtr(G, I->VR);

    /* spheres for stick & balls */
    if(stick_ball) {
      stick_ball_ratio =
        SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_ball_ratio);
      stick_ball_color =
        SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_stick_ball_color);
    } else if(draw_quality) {
      stick_ball = true;
    }
    if(stick_ball) {
      int ds;

      ds = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_sphere_quality);
      if(ds < 0)
        ds = 0;
      if(ds > 4)
        ds = 4;

      if(draw_quality && (ds < 3))
        ds = 3;

      sp = G->Sphere->Sphere[ds];

      I->SP = sp;
      I->VSP = Alloc(float, maxCyl * 2 * (3 + sp->NVertTot * 6));
      I->VSPC = Alloc(float, maxCyl * 2 * 7);
      I->VarAlphaSph = Alloc(float, maxCyl * 2);
      ErrChkPtr(G, I->VSP);
    }
    I->NEdge = nEdge;
    if (adjacent_atoms){
      RepCylBondPopulateAdjacentAtoms(adjacent_atoms, obj, cs, marked);
    }
    vr = I->VR;
    vsp = I->VSP;
    vspc = I->VSPC;
    b = obj->Bond;
    for(a = 0; a < obj->NBond; a++) {
      b1 = b->index[0];
      b2 = b->index[1];
      ord = b->order;
      if(obj->DiscreteFlag) {
        if((cs == obj->DiscreteCSet[b1]) && (cs == obj->DiscreteCSet[b2])) {
          a1 = obj->DiscreteAtmToIdx[b1];
          a2 = obj->DiscreteAtmToIdx[b2];
        } else {
          a1 = -1;
          a2 = -1;
        }
      } else {
        a1 = cs->AtmToIdx[b1];
        a2 = cs->AtmToIdx[b2];
      }
      if((a1 >= 0) && (a2 >= 0)) {
        register AtomInfoType *ati1 = obj->AtomInfo + b1;
        register AtomInfoType *ati2 = obj->AtomInfo + b2;
        int bd_stick_color;
        float bd_radius, bd_radius_full;
        float overlap_r, nub_r;
        float bd_transp;
	float bd_alpha = 0.f;
	int capdraw1 = 0, capdraw2 = 0;
	int adj1 = 0, adj2 = 0;
	if (adjacent_atoms){
	  if (adjacent_atoms[a1] != NULL){
	    adj1 = adjacent_atoms[a1][0];
	  }
	  if (adjacent_atoms[a2] != NULL){
	    adj2 = adjacent_atoms[a2][0];
	  }
	}
        AtomInfoGetBondSetting_color(G, b, cSetting_stick_color, stick_color,
                                     &bd_stick_color);
        AtomInfoGetBondSetting_f(G, b, cSetting_stick_radius, radius, &bd_radius);
        if(variable_alpha){
          AtomInfoGetBondSetting_f(G, b, cSetting_stick_transparency, transp, &bd_transp);
	  bd_alpha = (1.0F - bd_transp);
	}
        bd_radius_full = fabs(bd_radius);
        if(bd_radius < 0.0F) {
          bd_radius = -bd_radius;
          if((ati1->protons == cAN_H) || (ati2->protons == cAN_H))
            bd_radius = bd_radius * h_scale;    /* scaling for bonds involving hydrogen */

        }
        overlap_r = overlap * bd_radius;
        nub_r = nub * bd_radius;

        if(bd_stick_color < 0) {
          if(bd_stick_color == cColorObject) {
            c1 = (c2 = obj->Obj.Color);
          } else if(ColorCheckRamped(G, bd_stick_color)) {
            c1 = (c2 = bd_stick_color);
          } else {
            c1 = *(cs->Color + a1);
            c2 = *(cs->Color + a2);
          }
        } else {
          c1 = (c2 = bd_stick_color);
        }
        vv1 = cs->Coord + 3 * a1;
        vv2 = cs->Coord + 3 * a2;

        s1 = ati1->visRep[cRepCyl];
        s2 = ati2->visRep[cRepCyl];

        if(!(s1 && s2))
          if(!half_bonds) {
            s1 = 0;
            s2 = 0;
          }

        if(hide_long && (s1 || s2)) {
          float cutoff = (ati1->vdw + ati2->vdw) * _0p9;
          ai1 = obj->AtomInfo + b1;
          ai2 = obj->AtomInfo + b2;
          if(!within3f(vv1, vv2, cutoff))       /* atoms separated by more than 90% of the sum of their vdw radii */
            s1 = s2 = 0;
        }
        if((!ati1->hetatm) && (!ati2->hetatm) &&
           ((cartoon_side_chain_helper && ati1->visRep[cRepCartoon]
             && ati2->visRep[cRepCartoon]) || (ribbon_side_chain_helper
                                               && ati1->visRep[cRepRibbon]
                                               && ati2->visRep[cRepRibbon]))) {
	  RepCylBondFilterBond(marked, ati1, ati2, b1, b2, na_mode, &c1, &c2, &s1, &s2);
        }
	
        if(stick_ball) {
          int d, e;
          if(stick_ball_ratio >= 1.0F)  /* don't use caps if spheres are big enough */
            caps_req = false;
          if(s1 && (!marked[b1])) {     /* just once for each atom... */
            int *q = sp->Sequence;
            int *s = sp->StripLen;
            float vdw =
              stick_ball_ratio * ((ati1->protons == cAN_H) ? bd_radius : bd_radius_full);
            float vdw1 = (vdw >= 0) ? vdw : -ati1->vdw * vdw;
            int sbc1 = (stick_ball_color == cColorDefault) ? c1 : stick_ball_color;
            if(sbc1 == cColorAtomic)
              sbc1 = ati1->color;
            marked[b1] = 1;
	    if(ColorCheckRamped(G, sbc1)) {
	      ColorGetRamped(G, sbc1, vv1, rgb2_buf, state);
	      rgb1 = rgb1_buf;
	    } else {
	      rgb1 = ColorGet(G, sbc1);
	    }
            copy3f(rgb1, vsp);
            vsp += 3;
            for(d = 0; d < sp->NStrip; d++) {
              for(e = 0; e < (*s); e++) {
                *(vsp++) = sp->dot[*q][0];      /* normal */
                *(vsp++) = sp->dot[*q][1];
                *(vsp++) = sp->dot[*q][2];
                *(vsp++) = vv1[0] + vdw1 * sp->dot[*q][0];      /* point */
                *(vsp++) = vv1[1] + vdw1 * sp->dot[*q][1];
                *(vsp++) = vv1[2] + vdw1 * sp->dot[*q][2];
                q++;
              }
              s++;
            }
            I->NSP++;
            copy3f(rgb1, vspc);
            vspc += 3;
            copy3f(vv1, vspc);
            vspc += 3;
            *(vspc++) = vdw1;
            I->NSPC++;
          }

          if(s2 && !(marked[b2])) {     /* just once for each atom..., SAME CODE as for s1, consolidate? */
            int *q = sp->Sequence;
            int *s = sp->StripLen;
            float vdw =
              stick_ball_ratio * ((ati2->protons == cAN_H) ? bd_radius : bd_radius_full);
            float vdw2 = (vdw >= 0) ? vdw : -ati2->vdw * vdw;
            int sbc2 = (stick_ball_color == cColorDefault) ? c2 : stick_ball_color;
            if(sbc2 == cColorAtomic)
              sbc2 = ati2->color;
            marked[b2] = 1;
            if(ColorCheckRamped(G, sbc2)) {
              ColorGetRamped(G, sbc2, vv2, rgb2_buf, state);
              rgb2 = rgb2_buf;
            } else {
              rgb2 = ColorGet(G, sbc2);
            }
            copy3f(rgb2, vsp);
            vsp += 3;
            for(d = 0; d < sp->NStrip; d++) {
              for(e = 0; e < (*s); e++) {
                *(vsp++) = sp->dot[*q][0];      /* normal */
                *(vsp++) = sp->dot[*q][1];
                *(vsp++) = sp->dot[*q][2];
                *(vsp++) = vv2[0] + vdw2 * sp->dot[*q][0];      /* point */
                *(vsp++) = vv2[1] + vdw2 * sp->dot[*q][1];
                *(vsp++) = vv2[2] + vdw2 * sp->dot[*q][2];
                q++;
              }
              s++;
            }
            I->NSP++;
            copy3f(rgb2, vspc);
            vspc += 3;
            copy3f(vv2, vspc);
            vspc += 3;
            *(vspc++) = vdw2;
            I->NSPC++;
          }
        }

	{
	  float alp;
	  if((alpha == 1.0) && (!variable_alpha)) {
	    alp = 1.0F;
	  } else if(variable_alpha) {
	    alp = bd_alpha;
	  } else {
	    alp = alpha;
	  }
	  CGOAlpha(Vcgo, alp);
	}

        if(hide_long && (s1 || s2)) {
          float cutoff = (ati1->vdw + ati2->vdw) * _0p9;
          ai1 = obj->AtomInfo + b1;
          ai2 = obj->AtomInfo + b2;
          if(!within3f(vv1, vv2, cutoff))       /* atoms separated by more than 90% of the sum of their vdw radii */
            s1 = s2 = 0;
        }

        if(s1 || s2) {
          int bd_valence_flag;
	  n_bonds++;
          AtomInfoGetBondSetting_b(G, b, cSetting_valence, valence_flag,
                                   &bd_valence_flag);

          if((bd_valence_flag) && (ord > 1) && (ord < 5)) {

            if((c1 == c2) && s1 && s2 && (!ColorCheckRamped(G, c1))) {
              v0 = ColorGet(G, c1);
              RepValence(I, Vcgo, &I->N,
                         &vr, &I->NR,
                         vv1, vv2, other,
                         a1, a2, cs->Coord,
                         v0, NULL, ord, nEdge,
                         bd_radius, overlap, nub, false, fixed_radius, scale_r, shader_mode);
            } else {
              rgb1 = NULL;
              if(s1) {
                if(ColorCheckRamped(G, c1)) {
                  ColorGetRamped(G, c1, vv1, rgb1_buf, state);
                } else {
                  rgb1 = ColorGet(G, c1);
                  copy3f(rgb1, rgb1_buf);
                }
                rgb1 = rgb1_buf;
              }

              rgb2 = NULL;
              if(s2) {
                if(ColorCheckRamped(G, c2)) {
                  ColorGetRamped(G, c2, vv2, rgb2_buf, state);
                } else {
                  rgb2 = ColorGet(G, c2);
                  copy3f(rgb2, rgb2_buf);
                }
                rgb2 = rgb2_buf;
              }
              RepValence(I, Vcgo, &I->N,
                         &vr, &I->NR,
                         vv1, vv2, other,
                         a1, a2, cs->Coord,
                         rgb1, rgb2, ord, nEdge,
                         bd_radius, overlap, nub, true, fixed_radius, scale_r, shader_mode);
            }

          } else {
	    float *cv2 = NULL;
	    if((c1 == c2) && s1 && s2 && (!ColorCheckRamped(G, c1))) {
	    } else {
	      cv2 = Alloc(float, 3);
	      copy3f(ColorGet(G, c2), cv2);
	    }
	    copy3f(vv1, v1);
	    copy3f(vv2, v2);
	    
	    v0 = ColorGet(G, c1);
	    
	    /* ray-tracing */
	    
	    copy3f(v0, vr);
	    vr += 3;
	    if (cv2){
	      copy3f(cv2, vr);
	    } else {
	      copy3f(v0, vr);
	    }
	    vr += 3;
	    
	    *(vr++) = bd_radius;
	    
	    copy3f(v1, vr);
	    vr += 3;
	    
	    copy3f(v2, vr);
	    vr += 3;
	    
	    I->NR++;
	    
	    /* store color */
	    CGOColorv(Vcgo, v0);
	    I->N++;
	    /* generate a cylinder */
	    if (adjacent_atoms){
	      capdraw1 = (adj1==1);
	      capdraw2 = (adj2==1);
	    } else {
	      capdraw1 = caps_req;		
	      capdraw2 = caps_req;		
	    }
	    if (shader_mode){
	      if (capdraw1){
		if (capdrawn[a1]){ capdraw1 = 0; }
		else { capdrawn[a1] = 1; }
	      }
	      if (capdraw2){
		if (capdrawn[a2]) { capdraw2 = 0; }
		else { capdrawn[a2] = 1; }
	      }
	    }
	    RepCylinder(G, I, Vcgo, v1, v2, nEdge, capdraw1, capdraw2, bd_radius, overlap_r,
			nub_r, NULL, shader_mode, cv2);
	    if (cv2){
	      FreeP(cv2);
	    }
	  }
          if(variable_alpha) {  /* record alpha values for each */
            float bd_alpha = (1.0F - bd_transp);
            while(n_var_alpha < I->N) {
              I->VarAlpha[n_var_alpha++] = bd_alpha;
            }
            while(n_var_alpha_ray < I->NR) {
              I->VarAlphaRay[n_var_alpha_ray++] = bd_alpha;
            }
            while(n_var_alpha_sph < I->NSP) {
              I->VarAlphaSph[n_var_alpha_sph++] = bd_alpha;
            }
          }
        }
      }
      /*      printf("%d\n",(v-I->V)/( (9+6+6) * (nEdge+1) + 3 )); */
      b++;
    }


    /* Now we need to patch/close the areas where cylinders missed for stick_good_geometry for each atom */
    if (false && adjacent_atoms){
      float stick_radius;
      stick_radius = SettingGetGlobal_f(G, cSetting_stick_radius);      
      for(a = 0; a < obj->NAtom; a++) {    
	if (adjacent_atoms[a] != NULL){
	  switch (adjacent_atoms[a][0]){
	  case 2:
	    a1 =  adjacent_atoms[a][1];
	    a2 =  adjacent_atoms[a][2];
	    //	    printf("# adjacent atoms=2 : atom#%d adjacents : %d %d\n", a, adjacent_atoms[a][1], adjacent_atoms[a][2]);
	    {
	      float *atomPoint, *atomPoint1, *atomPoint2, bondDirection1[3], bondDirection2[3], cross[3], white[] = { 1.f, 1.f, 1.f }, pt[3];
	      /*	      register AtomInfoType *ati = obj->AtomInfo + a;
			      register AtomInfoType *ati1 = obj->AtomInfo + adjacent_atoms[a][1];
			      register AtomInfoType *ati2 = obj->AtomInfo + adjacent_atoms[a][2];*/
	      atomPoint = cs->Coord + 3 * a;
	      atomPoint1 = cs->Coord + 3 * a1;
	      atomPoint2 = cs->Coord + 3 * a2;
	      bondDirection1[0] = atomPoint1[0] - atomPoint[0];
	      bondDirection1[1] = atomPoint1[1] - atomPoint[1];
	      bondDirection1[2] = atomPoint1[2] - atomPoint[2];
	      bondDirection2[0] = atomPoint2[0] - atomPoint[0];
	      bondDirection2[1] = atomPoint2[1] - atomPoint[1];
	      bondDirection2[2] = atomPoint2[2] - atomPoint[2];

	      cross_product3f(bondDirection1, bondDirection2, cross);
	      normalize3f(cross);
	      CGOColorv(Vcgo, white);
	      CGOBegin(Vcgo, GL_TRIANGLES);
	      pt[0] = atomPoint[0] + cross[0] * stick_radius;
	      pt[1] = atomPoint[1] + cross[1] * stick_radius;
	      pt[2] = atomPoint[2] + cross[2] * stick_radius;
	      CGOVertexv(Vcgo, pt);	      
	      pt[0] = atomPoint[0] - cross[0] * stick_radius;
	      pt[1] = atomPoint[1] - cross[1] * stick_radius;
	      pt[2] = atomPoint[2] - cross[2] * stick_radius;
	      CGOVertexv(Vcgo, pt);	      
	      pt[0] = - (bondDirection1[0] + bondDirection2[0]);
	      pt[1] = - (bondDirection1[1] + bondDirection2[1]);
	      pt[2] = - (bondDirection1[2] + bondDirection2[2]);
	      normalize3f(pt);
	      pt[0] = pt[0] * stick_radius + atomPoint[0];
	      pt[1] = pt[1] * stick_radius + atomPoint[1];
	      pt[2] = pt[2] * stick_radius + atomPoint[2];
	      CGOVertexv(Vcgo, pt);	      
	      CGOEnd(Vcgo);
	    }
	    break;
	  }
	}
      }
    }

    CGOStop(Vcgo);
#ifdef _PYMOL_CGO_DRAWARRAYS
    if (Vcgo){
      CGO *convertcgo = NULL;
      convertcgo = CGOCombineBeginEnd(Vcgo, 0);    
      CGOFree(Vcgo);    
      Vcgo = 0;
      I->Vcgo = convertcgo;
    }
#endif

    /*    PRINTFD(G, FB_RepCylBond)
	  " RepCylBond-DEBUG: %d triplets\n", (int) (vptr - I->V) / 3 ENDFD;*/

    /*    if((signed) v_size < (vptr - I->V))
      ErrFatal(G, "RepCylBond", "V array overrun.");
    */
    if((signed) vr_size < (vr - I->VR))
      ErrFatal(G, "RepCylBond", "VR array overrun.");

    //    VLASizeForSure(I->V, float, (vptr - I->V));
    I->VR = ReallocForSure(I->VR, float, (vr - I->VR));
    if(I->VSP)
      I->VSP = ReallocForSure(I->VSP, float, (vsp - I->VSP));
    if(I->VSPC)
      I->VSPC = ReallocForSure(I->VSPC, float, (vspc - I->VSPC));
    if(I->VarAlpha)
      I->VarAlpha = ReallocForSure(I->VarAlpha, float, n_var_alpha);
    if(n_var_alpha_ray) {
      if(I->VarAlphaRay)
        I->VarAlphaRay = ReallocForSure(I->VarAlphaRay, float, n_var_alpha_ray);
    } else {
      FreeP(I->VarAlphaRay);
    }
    if(n_var_alpha_sph) {
      if(I->VarAlphaSph)
        I->VarAlphaSph = ReallocForSure(I->VarAlphaSph, float, n_var_alpha_sph);
    } else {
      FreeP(I->VarAlphaSph);
    }

    /* Generating the picking CGO I->VPcgo */
    if(SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_pickable)) {
      I->VPcgo = CGONew(G);      

      PRINTFD(G, FB_RepCylBond)
        " RepCylBondNEW: generating pickable version\n" ENDFD;

      /* pickable versions are simply capped boxes
       */
      b = obj->Bond;
      for(a = 0; a < obj->NBond; a++) {
        b1 = b->index[0];
        b2 = b->index[1];
        if(obj->DiscreteFlag) {
          if((cs == obj->DiscreteCSet[b1]) && (cs == obj->DiscreteCSet[b2])) {
            a1 = obj->DiscreteAtmToIdx[b1];
            a2 = obj->DiscreteAtmToIdx[b2];
          } else {
            a1 = -1;
            a2 = -1;
          }
        } else {
          a1 = cs->AtmToIdx[b1];
          a2 = cs->AtmToIdx[b2];
        }
        if((a1 >= 0) && (a2 >= 0)) {
          ai1 = obj->AtomInfo + b1;
          ai2 = obj->AtomInfo + b2;
          s1 = ai1->visRep[cRepCyl];
          s2 = ai2->visRep[cRepCyl];

          if(!(s1 && s2)) {
            if(!half_bonds) {
              s1 = 0;
              s2 = 0;
            }
          }

          if(s1 || s2) {
            float bd_radius;
            float overlap_r, nub_r;

            AtomInfoGetBondSetting_f(G, b, cSetting_stick_radius, radius, &bd_radius);

            overlap_r = overlap * bd_radius;
            nub_r = nub * bd_radius;

            copy3f(cs->Coord + 3 * a1, v1);
            copy3f(cs->Coord + 3 * a2, v2);

            h[0] = (v1[0] + v2[0]) / 2;
            h[1] = (v1[1] + v2[1]) / 2;
            h[2] = (v1[2] + v2[2]) / 2;
            if(s1 & (!ai1->masked)) {
	      CGOPickColor(I->VPcgo, b1, a);
              RepCylinderBox(I, I->VPcgo, v1, h, bd_radius, overlap_r, nub_r);
            }
            if(s2 & (!ai2->masked)) {
	      CGOPickColor(I->VPcgo, b2, a);
              RepCylinderBox(I, I->VPcgo, h, v2, bd_radius, overlap_r, nub_r);
            }
          }
        }
        b++;
      }

      CGOStop(I->VPcgo);

#ifdef _PYMOL_CGO_DRAWARRAYS
      {
	CGO *convertcgo = CGOCombineBeginEnd(I->VPcgo, 0);    
	CGOFree(I->VPcgo);    
	I->VPcgo = convertcgo;

      }
#endif
    }
  }
  FreeP(other);
  FreeP(marked);
  FreeP(capdrawn);

  if (adjacent_atoms){
    for (a = 0; a < obj->NAtom ; a++){
      if (adjacent_atoms[a]){
	FreeP(adjacent_atoms[a]);
	adjacent_atoms[a] = 0;
      }
    }
    FreeP(adjacent_atoms);
  }
  return ((void *) (struct Rep *) I);
}

void RepCylinderBox(RepCylBond *I, CGO *cgo, float *vv1, float *vv2,
		    float tube_size, float overlap, float nub)
{

  float d[3], t[3], p0[3], p1[3], p2[3], n[3];
  float v[24], v1[3], v2[3];

  tube_size *= 0.7F;

  overlap += (nub / 2);

  /* direction vector */

  subtract3f(vv2, vv1, p0);

  normalize3f(p0);

  v1[0] = vv1[0] - p0[0] * overlap;
  v1[1] = vv1[1] - p0[1] * overlap;
  v1[2] = vv1[2] - p0[2] * overlap;

  v2[0] = vv2[0] + p0[0] * overlap;
  v2[1] = vv2[1] + p0[1] * overlap;
  v2[2] = vv2[2] + p0[2] * overlap;

  d[0] = (v2[0] - v1[0]);
  d[1] = (v2[1] - v1[1]);
  d[2] = (v2[2] - v1[2]);

  get_divergent3f(d, t);

  cross_product3f(d, t, p1);

  normalize3f(p1);

  cross_product3f(d, p1, p2);

  normalize3f(p2);

  /* now we have a coordinate system */

  n[0] = p1[0] * tube_size * (-1) + p2[0] * tube_size * (-1);
  n[1] = p1[1] * tube_size * (-1) + p2[1] * tube_size * (-1);
  n[2] = p1[2] * tube_size * (-1) + p2[2] * tube_size * (-1);

  v[0] = v1[0] + n[0];
  v[1] = v1[1] + n[1];
  v[2] = v1[2] + n[2];

  v[3] = v[0] + d[0];
  v[4] = v[1] + d[1];
  v[5] = v[2] + d[2];

  n[0] = p1[0] * tube_size * (1) + p2[0] * tube_size * (-1);
  n[1] = p1[1] * tube_size * (1) + p2[1] * tube_size * (-1);
  n[2] = p1[2] * tube_size * (1) + p2[2] * tube_size * (-1);

  v[6] = v1[0] + n[0];
  v[7] = v1[1] + n[1];
  v[8] = v1[2] + n[2];

  v[9] = v[6] + d[0];
  v[10] = v[7] + d[1];
  v[11] = v[8] + d[2];

  n[0] = p1[0] * tube_size * (1) + p2[0] * tube_size * (1);
  n[1] = p1[1] * tube_size * (1) + p2[1] * tube_size * (1);
  n[2] = p1[2] * tube_size * (1) + p2[2] * tube_size * (1);

  v[12] = v1[0] + n[0];
  v[13] = v1[1] + n[1];
  v[14] = v1[2] + n[2];

  v[15] = v[12] + d[0];
  v[16] = v[13] + d[1];
  v[17] = v[14] + d[2];

  n[0] = p1[0] * tube_size * (-1) + p2[0] * tube_size * (1);
  n[1] = p1[1] * tube_size * (-1) + p2[1] * tube_size * (1);
  n[2] = p1[2] * tube_size * (-1) + p2[2] * tube_size * (1);

  v[18] = v1[0] + n[0];
  v[19] = v1[1] + n[1];
  v[20] = v1[2] + n[2];

  v[21] = v[18] + d[0];
  v[22] = v[19] + d[1];
  v[23] = v[20] + d[2];

  CGOBegin(cgo, GL_TRIANGLE_STRIP);
  CGOVertexv(cgo, v);  
  CGOVertexv(cgo, &v[3]);  
  CGOVertexv(cgo, &v[6]);  
  CGOVertexv(cgo, &v[9]);  
  CGOVertexv(cgo, &v[12]);  
  CGOVertexv(cgo, &v[15]);  
  CGOVertexv(cgo, &v[18]);  
  CGOVertexv(cgo, &v[21]);  
  CGOVertexv(cgo, &v[0]);
  CGOVertexv(cgo, &v[3]);
  CGOEnd(cgo);

  CGOBegin(cgo, GL_TRIANGLE_STRIP);
  CGOVertexv(cgo, &v[0]);
  CGOVertexv(cgo, &v[6]);
  CGOVertexv(cgo, &v[18]);
  CGOVertexv(cgo, &v[12]);
  CGOEnd(cgo);

  CGOBegin(cgo, GL_TRIANGLE_STRIP);
  CGOVertexv(cgo, &v[3]);
  CGOVertexv(cgo, &v[9]);
  CGOVertexv(cgo, &v[21]);
  CGOVertexv(cgo, &v[15]);
  CGOEnd(cgo);
}

void RepCylinder(PyMOLGlobals *G, RepCylBond *I, CGO *cgo, float *v1arg, float *v2arg, int nEdge,
		 int frontCapArg, int endCapArg, float tube_size, float overlap, float nub, float **dir,
		 int shader_mode, float *v2color )
{

  float d[3], t[3], p0[3], p1[3], p2[3], v1[3], v2[3], x, y, c, cincr, cinit, normal[3], vertex1[3], vertex2[3];
  int frontCap = frontCapArg, endCap = endCapArg, tmpCap;
  int stick_round_nub = 0;

  if ( shader_mode ) { // GLSL
    //short cap = (endCap || frontCap) ? 1 : 0;
    short cap = (frontCap > 0 ? 5 : 0) | (endCap > 0 ? 10 : 0);

    // These are not really triangles, we are simply passing
    // origin, axis and flags information to CGO    
    vertex1[0] = v2arg[0]-v1arg[0];
    vertex1[1] = v2arg[1]-v1arg[1];
    vertex1[2] = v2arg[2]-v1arg[2];

    if (v2color){
      CGOShaderCylinder2ndColor(cgo, v1arg, vertex1, tube_size, cap + 16, v2color);
    } else {
      CGOShaderCylinder(cgo, v1arg, vertex1, tube_size, cap);
    }
    return;
  }

  if (G){
    stick_round_nub = SettingGetGlobal_i(G, cSetting_stick_round_nub);
  }
  /* direction vector */

  p0[0] = (v2arg[0] - v1arg[0]);
  p0[1] = (v2arg[1] - v1arg[1]);
  p0[2] = (v2arg[2] - v1arg[2]);

  normalize3f(p0);

  v1arg[0] -= p0[0] * overlap;
  v1arg[1] -= p0[1] * overlap;
  v1arg[2] -= p0[2] * overlap;

  if(endCap) {
    v2arg[0] += p0[0] * overlap;
    v2arg[1] += p0[1] * overlap;
    v2arg[2] += p0[2] * overlap;
  }

  d[0] = (v2arg[0] - v1arg[0]);
  d[1] = (v2arg[1] - v1arg[1]);
  d[2] = (v2arg[2] - v1arg[2]);

  v1[0] = v1arg[0]; v1[1] = v1arg[1]; v1[2] = v1arg[2]; 
  v2[0] = v2arg[0]; v2[1] = v2arg[1]; v2[2] = v2arg[2]; 
  if (dir){
    if (!*dir){
      *dir = Alloc(float, 3);
      (*dir)[0] = d[0]; (*dir)[1] = d[1]; (*dir)[2] = d[2];
    } else {
      if (get_angle3f(d, *dir)>=(cPI/2.)){
	v2[0] = v1arg[0]; v2[1] = v1arg[1]; v2[2] = v1arg[2]; 
	v1[0] = v2arg[0]; v1[1] = v2arg[1]; v1[2] = v2arg[2]; 
	d[0] = -d[0];     d[1] = -d[1];     d[2] = -d[2];
	tmpCap = frontCap;
	frontCap = endCap;
	endCap = tmpCap;
	p0[0] = (v2[0] - v1[0]);
	p0[1] = (v2[1] - v1[1]);
	p0[2] = (v2[2] - v1[2]);
	normalize3f(p0);
      }
    }
  }
  get_divergent3f(d, t);

  cross_product3f(d, t, p1);

  normalize3f(p1);

  cross_product3f(d, p1, p2);

  normalize3f(p2);

 /* now we have a coordinate system */

  if(frontCap) {
    if (stick_round_nub){
      int ntoskipattop = 0;
      int d, nverts = 0, cmax = 1 + (nEdge)/2;
      float prev ;
      float p3[3];
      float z1, z2;
      /* p1 is x coord in cap space */
      /* p2 is y coord in cap space */
      /* p3 is z coord in cap space (i.e., normal of cap */

      CGOBegin(cgo, GL_TRIANGLE_STRIP);

      p3[0] = -p0[0];
      p3[1] = -p0[1];
      p3[2] = -p0[2];
      z1 = z2 = 1.;

      if (nEdge % 2){
	cincr = (nEdge/2.)/(float)cmax;
	cinit = cincr + ntoskipattop;
      } else {
	cinit = 1. + ntoskipattop;
	cincr = 1.;
      }
      prev = cinit;

      for (c = cinit; c < cmax; c += cincr){
	z1 = z2;
	z2 = (float) cos((c) * PI / ((float)nEdge));
	for (d = 0; d <= nEdge; d++){
	  x = (float) cos((d) * 2 * PI / (float)nEdge) * sin((c-prev) * PI / (float)nEdge);
	  y = (float) sin((d) * 2 * PI / (float)nEdge) * sin((c-prev) * PI / (float)nEdge);
	  normal[0] = p1[0] * x + p2[0] * y + p3[0] * z1;
	  normal[1] = p1[1] * x + p2[1] * y + p3[1] * z1;
	  normal[2] = p1[2] * x + p2[2] * y + p3[2] * z1;
	  vertex1[0] = v1[0] + normal[0] * tube_size;
	  vertex1[1] = v1[1] + normal[1] * tube_size;
	  vertex1[2] = v1[2] + normal[2] * tube_size;
	  normalize3f(normal);
	  CGONormalv(cgo, normal);	  
	  CGOVertexv(cgo, vertex1);

	  x = (float) cos((d) * 2 * PI / (float)nEdge) * sin((c) * PI / (float)nEdge);
	  y = (float) sin((d) * 2 * PI / (float)nEdge) * sin((c) * PI / (float)nEdge);

	  normal[0] = p1[0] * x + p2[0] * y + p3[0] * z2;
	  normal[1] = p1[1] * x + p2[1] * y + p3[1] * z2;
	  normal[2] = p1[2] * x + p2[2] * y + p3[2] * z2;
	  vertex1[0] = v1[0] + normal[0] * tube_size;
	  vertex1[1] = v1[1] + normal[1] * tube_size;
	  vertex1[2] = v1[2] + normal[2] * tube_size;
	  normalize3f(normal);
	  CGONormalv(cgo, normal);	  
	  CGOVertexv(cgo, vertex1);
	  nverts += 2;
	}
	prev = cincr;
      }
      CGOEnd(cgo);
    } else {
      /* pointed front cap */
      CGOBegin(cgo, GL_TRIANGLE_FAN);

      /* normal for top of cap */
      normal[0] = -p0[0];
      normal[1] = -p0[1];
      normal[2] = -p0[2];
      /* vertex for top of cap */
      vertex1[0] = v1[0] - p0[0] * nub;
      vertex1[1] = v1[1] - p0[1] * nub;
      vertex1[2] = v1[2] - p0[2] * nub;
      CGONormalv(cgo, normal);
      CGOVertexv(cgo, vertex1);
      for(c = nEdge; c >= 0; c--) {
	/* this is a fan for the cap, 
	   around the outside edge of the end of the cylinder */
	x = (float) cos(c * 2 * PI / nEdge);
	y = (float) sin(c * 2 * PI / nEdge);
	/* normal for vertex in fan */
	normal[0] = p1[0] * x + p2[0] * y;
	normal[1] = p1[1] * x + p2[1] * y;
	normal[2] = p1[2] * x + p2[2] * y; 
	/* vertex for fan triangle*/
	vertex1[0] = v1[0] + normal[0] * tube_size;
	vertex1[1] = v1[1] + normal[1] * tube_size;
	vertex1[2] = v1[2] + normal[2] * tube_size;
	CGONormalv(cgo, normal);
	CGOVertexv(cgo, vertex1);
      }
      CGOEnd(cgo);
    }
  }

  if (v2color){
    float hd[3];
    mult3f(d, .5f, hd);
    CGOBegin(cgo, GL_TRIANGLE_STRIP);
    
    for(c = nEdge; c >= 0; c--) {
      x = (float) cos(c * 2 * PI / nEdge);
      y = (float) sin(c * 2 * PI / nEdge);
      normal[0] = p1[0] * tube_size * x + p2[0] * tube_size * y;
      normal[1] = p1[1] * tube_size * x + p2[1] * tube_size * y;
      normal[2] = p1[2] * tube_size * x + p2[2] * tube_size * y;
      
      vertex1[0] = v1[0] + normal[0];
      vertex1[1] = v1[1] + normal[1];
      vertex1[2] = v1[2] + normal[2];
      
      vertex2[0] = vertex1[0] + hd[0];
      vertex2[1] = vertex1[1] + hd[1];
      vertex2[2] = vertex1[2] + hd[2];
      normalize3f(normal);
      CGONormalv(cgo, normal);    
      CGOVertexv(cgo, vertex1);
      CGOVertexv(cgo, vertex2);
    }
    CGOEnd(cgo);

    CGOColorv(cgo, v2color);

    CGOBegin(cgo, GL_TRIANGLE_STRIP);
    
    for(c = nEdge; c >= 0; c--) {
      x = (float) cos(c * 2 * PI / nEdge);
      y = (float) sin(c * 2 * PI / nEdge);
      normal[0] = p1[0] * tube_size * x + p2[0] * tube_size * y;
      normal[1] = p1[1] * tube_size * x + p2[1] * tube_size * y;
      normal[2] = p1[2] * tube_size * x + p2[2] * tube_size * y;
      
      vertex1[0] = v1[0] + normal[0] + hd[0];
      vertex1[1] = v1[1] + normal[1] + hd[1];
      vertex1[2] = v1[2] + normal[2] + hd[2];
      
      vertex2[0] = vertex1[0] + hd[0];
      vertex2[1] = vertex1[1] + hd[1];
      vertex2[2] = vertex1[2] + hd[2];
      normalize3f(normal);
      CGONormalv(cgo, normal);    
      CGOVertexv(cgo, vertex1);
      CGOVertexv(cgo, vertex2);
    }
    CGOEnd(cgo);

  } else {
    CGOBegin(cgo, GL_TRIANGLE_STRIP);
    
    for(c = nEdge; c >= 0; c--) {
      x = (float) cos(c * 2 * PI / nEdge);
      y = (float) sin(c * 2 * PI / nEdge);
      normal[0] = p1[0] * tube_size * x + p2[0] * tube_size * y;
      normal[1] = p1[1] * tube_size * x + p2[1] * tube_size * y;
      normal[2] = p1[2] * tube_size * x + p2[2] * tube_size * y;
      
      vertex1[0] = v1[0] + normal[0];
      vertex1[1] = v1[1] + normal[1];
      vertex1[2] = v1[2] + normal[2];
      
      vertex2[0] = vertex1[0] + d[0];
      vertex2[1] = vertex1[1] + d[1];
      vertex2[2] = vertex1[2] + d[2];
      normalize3f(normal);
      CGONormalv(cgo, normal);    
      CGOVertexv(cgo, vertex1);
      CGOVertexv(cgo, vertex2);
    }
    CGOEnd(cgo);
  }

  if(endCap) {
    if (stick_round_nub){
      int ntoskipattop = 0;
      int d, nverts = 0, cmax = 1 + nEdge/2;
      float prev ;
      float p3[3];
      float z1, z2;
      /* p1 is x coord in cap space */
      /* p2 is y coord in cap space */
      /* p3 is z coord in cap space (i.e., normal of cap */
      CGOBegin(cgo, GL_TRIANGLE_STRIP);

      p3[0] = p0[0];
      p3[1] = p0[1];
      p3[2] = p0[2];
      z1 = z2 = 1.;

      if (nEdge % 2){
	cincr = (nEdge/2.)/(float)cmax;
	cinit = cincr + ntoskipattop;
      } else {
	cinit = 1. + ntoskipattop;
	cincr = 1.;
      }
      prev = cinit;
      for (c = cinit; c < cmax; c+=cincr){
	z1 = z2;
	z2 = (float) cos((c) * PI / ((float)nEdge));
	for (d = 0; d <= nEdge; d++){
	  x = (float) cos((d) * 2 * PI / (float)nEdge) * sin((c-prev) * PI / (float)nEdge);
	  y = (float) sin((d) * 2 * PI / (float)nEdge) * sin((c-prev) * PI / (float)nEdge);
	  normal[0] = p1[0] * x + p2[0] * y + p3[0] * z1;
	  normal[1] = p1[1] * x + p2[1] * y + p3[1] * z1;
	  normal[2] = p1[2] * x + p2[2] * y + p3[2] * z1;
	  vertex1[0] = v2[0] + normal[0] * tube_size;
	  vertex1[1] = v2[1] + normal[1] * tube_size;
	  vertex1[2] = v2[2] + normal[2] * tube_size;
	  normalize3f(normal);
	  CGONormalv(cgo, normal);	  
	  CGOVertexv(cgo, vertex1);

	  x = (float) cos((d) * 2 * PI / (float)nEdge) * sin((c) * PI / (float)nEdge);
	  y = (float) sin((d) * 2 * PI / (float)nEdge) * sin((c) * PI / (float)nEdge);

	  normal[0] = p1[0] * x + p2[0] * y + p3[0] * z2;
	  normal[1] = p1[1] * x + p2[1] * y + p3[1] * z2;
	  normal[2] = p1[2] * x + p2[2] * y + p3[2] * z2;
	  vertex1[0] = v2[0] + normal[0] * tube_size;
	  vertex1[1] = v2[1] + normal[1] * tube_size;
	  vertex1[2] = v2[2] + normal[2] * tube_size;
	  normalize3f(normal);
	  CGONormalv(cgo, normal);	  
	  CGOVertexv(cgo, vertex1);

	  nverts += 2;
	}
	prev = cincr;
      }
      CGOEnd(cgo);
    } else {
      /* pointed end cap */
      CGOBegin(cgo, GL_TRIANGLE_FAN);
      /* normal for top of cap */
      normal[0] = p0[0];
      normal[1] = p0[1];
      normal[2] = p0[2];
      /* vertex for top of cap */    
      vertex1[0] = v2[0] + p0[0] * nub;
      vertex1[1] = v2[1] + p0[1] * nub;
      vertex1[2] = v2[2] + p0[2] * nub;
      CGONormalv(cgo, normal);
      CGOVertexv(cgo, vertex1);
      for(c = 0; c <= nEdge; c++) {
	/* this is a fan for the cap, 
	   around the outside edge of the end of the cylinder */
	x = (float) cos(c * 2 * PI / nEdge);
	y = (float) sin(c * 2 * PI / nEdge);
	/* normal for vertex in fan */
	normal[0] = p1[0] * x + p2[0] * y;
	normal[1] = p1[1] * x + p2[1] * y;
	normal[2] = p1[2] * x + p2[2] * y; 
	/* vertex for fan triangle*/      
	vertex1[0] = v2[0] + normal[0] * tube_size;
	vertex1[1] = v2[1] + normal[1] * tube_size;
	vertex1[2] = v2[2] + normal[2] * tube_size;
      
	CGONormalv(cgo, normal);
	CGOVertexv(cgo, vertex1);
      }
      CGOEnd(cgo);
    }
  }
}

static void RepCylinderImmediate(float *v1arg, float *v2arg, int nEdge,
                                 int frontCapArg, int endCapArg,
                                 float overlap, float nub, float radius, float **dir)
{
  float d[3], t[3], p0[3], p1[3], p2[3], v1ptr[3], v2ptr[3], *v1, *v2;
  float v[3], vv[3], vvv[3];
  float x, y;
  int c, frontCap = frontCapArg, endCap = endCapArg, tmpCap;

  p0[0] = (v2arg[0] - v1arg[0]);
  p0[1] = (v2arg[1] - v1arg[1]);
  p0[2] = (v2arg[2] - v1arg[2]);

  normalize3f(p0);

  v1ptr[0] = v1arg[0]; v1ptr[1] = v1arg[1]; v1ptr[2] = v1arg[2];
  v2ptr[0] = v2arg[0]; v2ptr[1] = v2arg[1]; v2ptr[2] = v2arg[2];

  v1ptr[0] -= p0[0] * overlap;
  v1ptr[1] -= p0[1] * overlap;
  v1ptr[2] -= p0[2] * overlap;

  if(endCap) {
    v2ptr[0] += p0[0] * overlap;
    v2ptr[1] += p0[1] * overlap;
    v2ptr[2] += p0[2] * overlap;
  }

  v1 = v1ptr;
  v2 = v2ptr;

  d[0] = (v2[0] - v1[0]);
  d[1] = (v2[1] - v1[1]);
  d[2] = (v2[2] - v1[2]);

  if (dir){
    if (!*dir){
      *dir = Alloc(float, 3);
      (*dir)[0] = d[0]; (*dir)[1] = d[1]; (*dir)[2] = d[2];
    } else {
      if (get_angle3f(d, *dir)>=(cPI/2.)){
	v1 = v2ptr;
	v2 = v1ptr;
	d[0] = -d[0];     d[1] = -d[1];     d[2] = -d[2];
	tmpCap = frontCap;
	frontCap = endCap;
	endCap = tmpCap;
      }
    }
  }

  /* direction vector */

  p0[0] = (v2[0] - v1[0]);
  p0[1] = (v2[1] - v1[1]);
  p0[2] = (v2[2] - v1[2]);

  normalize3f(p0);

  get_divergent3f(d, t);

  cross_product3f(d, t, p1);

  normalize3f(p1);

  cross_product3f(d, p1, p2);

  normalize3f(p2);

  /* now we have a coordinate system */
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
#ifdef _PYMOL_GL_DRAWARRAYS
  {
    int nverts = (nEdge+1) * 2;
    int pl;
    ALLOCATE_ARRAY(GLfloat,vertVals,nverts*3)
    ALLOCATE_ARRAY(GLfloat,normVals,nverts*3)
    pl = 0;
    for(c = nEdge; c >= 0; c--) {
      x = (float) radius * cos(c * 2 * PI / nEdge);
      y = (float) radius * sin(c * 2 * PI / nEdge);
      v[0] = p1[0] * x + p2[0] * y;
      v[1] = p1[1] * x + p2[1] * y;
      v[2] = p1[2] * x + p2[2] * y;
      
      vv[0] = v1[0] + v[0];
      vv[1] = v1[1] + v[1];
      vv[2] = v1[2] + v[2];
      
      vvv[0] = vv[0] + d[0];
      vvv[1] = vv[1] + d[1];
      vvv[2] = vv[2] + d[2];

      normVals[pl] = v[0]; normVals[pl+1] = v[1]; normVals[pl+2] = v[2];
      vertVals[pl] = vv[0]; vertVals[pl+1] = vv[1]; vertVals[pl+2] = vv[2];
      pl += 3;
      normVals[pl] = v[0]; normVals[pl+1] = v[1]; normVals[pl+2] = v[2];
      vertVals[pl] = vvv[0]; vertVals[pl+1] = vvv[1]; vertVals[pl+2] = vvv[2];
      pl += 3;
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertVals);
    glNormalPointer(GL_FLOAT, 0, normVals);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, nverts);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    DEALLOCATE_ARRAY(vertVals)
    DEALLOCATE_ARRAY(normVals)
  }
#else
  glBegin(GL_TRIANGLE_STRIP);

  for(c = nEdge; c >= 0; c--) {
    x = (float) radius * cos(c * 2 * PI / nEdge);
    y = (float) radius * sin(c * 2 * PI / nEdge);
    v[0] = p1[0] * x + p2[0] * y;
    v[1] = p1[1] * x + p2[1] * y;
    v[2] = p1[2] * x + p2[2] * y;

    vv[0] = v1[0] + v[0];
    vv[1] = v1[1] + v[1];
    vv[2] = v1[2] + v[2];

    glNormal3fv(v);

    vvv[0] = vv[0] + d[0];
    vvv[1] = vv[1] + d[1];
    vvv[2] = vv[2] + d[2];

    glVertex3fv(vv);
    glVertex3fv(vvv);
  }
  glEnd();
#endif
#endif

  if(frontCap) {
    v[0] = -p0[0];
    v[1] = -p0[1];
    v[2] = -p0[2];

    vv[0] = v1[0] - p0[0] * nub;
    vv[1] = v1[1] - p0[1] * nub;
    vv[2] = v1[2] - p0[2] * nub;

#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
#ifdef _PYMOL_GL_DRAWARRAYS
    {
      int nverts = nEdge+2, pl;
      ALLOCATE_ARRAY(GLfloat,vertVals,nverts*3)
      ALLOCATE_ARRAY(GLfloat,normVals,nverts*3)

      normVals[0] = v[0]; normVals[1] = v[1]; normVals[2] = v[2];
      vertVals[0] = vv[0]; vertVals[1] = vv[1]; vertVals[2] = vv[2];
      pl = 3;
      for(c = nEdge; c >= 0; c--) {
	x = (float) radius * cos(c * 2 * PI / nEdge);
	y = (float) radius * sin(c * 2 * PI / nEdge);
	v[0] = p1[0] * x + p2[0] * y;
	v[1] = p1[1] * x + p2[1] * y;
	v[2] = p1[2] * x + p2[2] * y;
	
	vv[0] = v1[0] + v[0];
	vv[1] = v1[1] + v[1];
	vv[2] = v1[2] + v[2];
	
	normVals[pl] = v[0]; normVals[pl+1] = v[1]; normVals[pl+2] = v[2];
	vertVals[pl++] = vv[0]; vertVals[pl++] = vv[1]; vertVals[pl++] = vv[2];
      }
      glEnableClientState(GL_VERTEX_ARRAY);
      glEnableClientState(GL_NORMAL_ARRAY);
      glVertexPointer(3, GL_FLOAT, 0, vertVals);
      glNormalPointer(GL_FLOAT, 0, normVals);
      glDrawArrays(GL_TRIANGLE_FAN, 0, nverts);
      glDisableClientState(GL_NORMAL_ARRAY);
      glDisableClientState(GL_VERTEX_ARRAY);
      DEALLOCATE_ARRAY(vertVals)
      DEALLOCATE_ARRAY(normVals)
    }
#else
    glBegin(GL_TRIANGLE_FAN);

    glNormal3fv(v);
    glVertex3fv(vv);

    for(c = nEdge; c >= 0; c--) {
      x = (float) radius * cos(c * 2 * PI / nEdge);
      y = (float) radius * sin(c * 2 * PI / nEdge);
      v[0] = p1[0] * x + p2[0] * y;
      v[1] = p1[1] * x + p2[1] * y;
      v[2] = p1[2] * x + p2[2] * y;

      vv[0] = v1[0] + v[0];
      vv[1] = v1[1] + v[1];
      vv[2] = v1[2] + v[2];

      glNormal3fv(v);
      glVertex3fv(vv);
    }

    glEnd();
#endif
#endif
  }

  if(endCap) {

    v[0] = p0[0];
    v[1] = p0[1];
    v[2] = p0[2];

    vv[0] = v2[0] + p0[0] * nub;
    vv[1] = v2[1] + p0[1] * nub;
    vv[2] = v2[2] + p0[2] * nub;

#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
#ifdef _PYMOL_GL_DRAWARRAYS
    {
      int nverts = nEdge+2, pl;
      ALLOCATE_ARRAY(GLfloat,vertVals,nverts*3)
      ALLOCATE_ARRAY(GLfloat,normVals,nverts*3)

      normVals[0] = v[0]; normVals[1] = v[1]; normVals[2] = v[2];
      vertVals[0] = vv[0]; vertVals[1] = vv[1]; vertVals[2] = vv[2];
      pl = 3;
      for(c = 0; c <= nEdge; c++) {
	x = (float) radius * cos(c * 2 * PI / nEdge);
	y = (float) radius * sin(c * 2 * PI / nEdge);
	v[0] = p1[0] * x + p2[0] * y;
	v[1] = p1[1] * x + p2[1] * y;
	v[2] = p1[2] * x + p2[2] * y;
	
	vv[0] = v2[0] + v[0];
	vv[1] = v2[1] + v[1];
	vv[2] = v2[2] + v[2];
	normVals[pl] = v[0]; normVals[pl+1] = v[1]; normVals[pl+2] = v[2];
	vertVals[pl++] = vv[0]; vertVals[pl++] = vv[1]; vertVals[pl++] = vv[2];
      }
      glEnableClientState(GL_VERTEX_ARRAY);
      glEnableClientState(GL_NORMAL_ARRAY);
      glVertexPointer(3, GL_FLOAT, 0, vertVals);
      glNormalPointer(GL_FLOAT, 0, normVals);
      glDrawArrays(GL_TRIANGLE_FAN, 0, nverts);
      glDisableClientState(GL_NORMAL_ARRAY);
      glDisableClientState(GL_VERTEX_ARRAY);
      DEALLOCATE_ARRAY(vertVals)
      DEALLOCATE_ARRAY(normVals)
    }
#else
    glBegin(GL_TRIANGLE_FAN);

    glNormal3fv(v);
    glVertex3fv(vv);

    for(c = 0; c <= nEdge; c++) {
      x = (float) radius * cos(c * 2 * PI / nEdge);
      y = (float) radius * sin(c * 2 * PI / nEdge);
      v[0] = p1[0] * x + p2[0] * y;
      v[1] = p1[1] * x + p2[1] * y;
      v[2] = p1[2] * x + p2[2] * y;

      vv[0] = v2[0] + v[0];
      vv[1] = v2[1] + v[1];
      vv[2] = v2[2] + v[2];
      glNormal3fv(v);
      glVertex3fv(vv);
    }
    glEnd();
#endif
#endif
  }
}

void RepCylBondRenderImmediate(CoordSet * cs, RenderInfo * info)
{
  /* performance optimized, so it does not support the following:

     - anything other than opengl
     - display of bond valences
     - per-bond & per-atom properties
     - half-bonds
     - helper settings such as cartoon_side_chain_helper
     - suppression of long bonds
     - color ramps
     - atom picking
     - display lists
     - transparency 

   */

  PyMOLGlobals *G = cs->State.G;
  if(info->ray || info->pick || (!(G->HaveGUI && G->ValidContext)))
    return;
  else {
    int active = false;
    ObjectMolecule *obj = cs->Obj;
    int nEdge = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_stick_quality);
    float radius =
      fabs(SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_radius));
    float overlap =
      SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_overlap);
    float nub = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_nub);
    float overlap_r = radius * overlap;
    float nub_r = radius * nub;

    {
      int a;
      int nBond = obj->NBond;
      BondType *bd = obj->Bond;
      AtomInfoType *ai = obj->AtomInfo;
      int *atm2idx = cs->AtmToIdx;
      int discreteFlag = obj->DiscreteFlag;
      int last_color = -9;
      float *coord = cs->Coord;
      const float _pt5 = 0.5F;

      for(a = 0; a < nBond; a++) {
        int b1 = bd->index[0];
        int b2 = bd->index[1];
        AtomInfoType *ai1, *ai2;
        bd++;

        if((ai1 = ai + b1)->visRep[cRepCyl] && (ai2 = ai + b2)->visRep[cRepCyl]) {
          int a1, a2;
          active = true;
          if(discreteFlag) {
            /* not optimized */
            if((cs == obj->DiscreteCSet[b1]) && (cs == obj->DiscreteCSet[b2])) {
              a1 = obj->DiscreteAtmToIdx[b1];
              a2 = obj->DiscreteAtmToIdx[b2];
            } else {
              a1 = -1;
              a2 = -1;
            }
          } else {
            a1 = atm2idx[b1];
            a2 = atm2idx[b2];
          }

          if((a1 >= 0) && (a2 >= 0)) {
            int c1 = ai1->color;
            int c2 = ai2->color;

            float *v1 = coord + 3 * a1;
            float *v2 = coord + 3 * a2;

            if(c1 == c2) {      /* same colors -> one cylinder */
              if(c1 != last_color) {
                last_color = c1;
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
                glColor3fv(ColorGet(G, c1));
#endif
              }

	      /* overlap is half since it is one cylinder representing both halfs of a bond */
              RepCylinderImmediate(v1, v2, nEdge, 1, 1, overlap_r, nub_r, radius, NULL);

            } else {            /* different colors -> two cylinders, no interior */
              float avg[3], *dir = NULL;

              avg[0] = (v1[0] + v2[0]) * _pt5;
              avg[1] = (v1[1] + v2[1]) * _pt5;
              avg[2] = (v1[2] + v2[2]) * _pt5;

              if(c1 != last_color) {
                last_color = c1;
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
                glColor3fv(ColorGet(G, c1));
#endif
              }

              RepCylinderImmediate(v1, avg, nEdge, 1, 0, overlap_r, nub_r, radius, &dir);

              if(c2 != last_color) {
                last_color = c2;
#ifdef PURE_OPENGL_ES_2
    /* TODO */
#else
                glColor3fv(ColorGet(G, c2));
#endif
              }

              RepCylinderImmediate(v2, avg, nEdge, 1, 0, overlap_r, nub_r, radius, &dir);
	      if (dir){
		FreeP(dir);
		dir = 0;
	      }
            }
          }
        }
      }
    }
    if(!active)
      cs->Active[cRepCyl] = false;
  }
}
